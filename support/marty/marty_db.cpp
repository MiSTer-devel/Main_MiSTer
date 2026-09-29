#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <string>
#include <vector>
#include <unordered_map>

#include "marty_db.h"

// ---------------------------------------------------------------------------
// Keys

static uint64_t fnv1a(const uint8_t *p, size_t n, uint64_t h = 0xcbf29ce484222325ULL)
{
	while (n--)
	{
		h ^= *p++;
		h *= 0x100000001b3ULL;
	}
	return h;
}

static std::string hex_key(const char *kind, uint64_t h)
{
	char buf[32];
	snprintf(buf, sizeof(buf), "%s:%016llx", kind, (unsigned long long)h);
	return buf;
}

// volume identifier, volume space size, creation date and time, as stored
std::string mdb_cd_key(const uint8_t *pvd)
{
	if (pvd[0] != 1 || memcmp(pvd + 1, "CD001", 5)) return "";
	uint64_t h = fnv1a(pvd + 40, 32);
	h = fnv1a(pvd + 80, 4, h);
	h = fnv1a(pvd + 813, 17, h);
	return hex_key("cd", h);
}

// YYYYMMDDHHMMSScc at byte 813; Towns discs were made from 1989 on, and some
// record zeros or a year like 2106
int mdb_cd_year(const uint8_t *pvd)
{
	int y = 0;
	for (int i = 0; i < 4; i++)
	{
		uint8_t c = pvd[813 + i];
		if (c < '0' || c > '9') return 0;
		y = y * 10 + (c - '0');
	}
	return (y >= 1986 && y <= 2005) ? y : 0;
}

std::string mdb_fd_key(const uint8_t *head, size_t len, uint64_t size)
{
	uint8_t le[8];
	for (int i = 0; i < 8; i++) le[i] = (uint8_t)(size >> (8 * i));
	return hex_key("fd", fnv1a(le, 8, fnv1a(head, len)));
}

// ASCII lower case, whitespace runs folded to one space, trimmed:
// "After Burner  (Japan)" -> "x:after burner (japan)"
std::string mdb_exact_key(const char *name)
{
	std::string s;
	bool space = false;
	for (const unsigned char *p = (const unsigned char *)name; *p; p++)
	{
		unsigned char c = *p;
		if (c == ' ' || c == '\t')
		{
			space = true;
			continue;
		}
		if (space && !s.empty()) s += ' ';
		space = false;
		s += (char)((c >= 'A' && c <= 'Z') ? c + 32 : c);
	}
	return s.empty() ? s : "x:" + s;
}

// Bracketed spans dropped, the words "the" and "and" dropped, Roman numerals
// II-X as digits, letters and digits kept:
// "The Legend of Kyrandia II (Japan)" -> "l:legendofkyrandia2"
std::string mdb_loose_key(const char *name)
{
	static const char *roman[][2] = {
		{ "ii", "2" }, { "iii", "3" }, { "iv", "4" }, { "v", "5" }, { "vi", "6" },
		{ "vii", "7" }, { "viii", "8" }, { "ix", "9" }, { "x", "10" } };

	std::string out, word;
	auto flush = [&]()
	{
		if (word != "the" && word != "and")
		{
			const char *w = word.c_str();
			for (auto &r : roman) if (!strcmp(w, r[0])) w = r[1];
			out += w;
		}
		word.clear();
	};

	size_t len = strlen(name);
	for (size_t i = 0; i < len;)
	{
		unsigned char c = name[i];
		if (c == '(' || c == '[')
		{
			const char *close = strchr(name + i + 1, c == '(' ? ')' : ']');
			i = close ? (size_t)(close - name) + 1 : len;
			flush();
			continue;
		}
		if (c >= 'A' && c <= 'Z') c += 32;
		if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) word += (char)c;
		else flush();
		i++;
	}
	flush();
	return out.empty() ? out : "l:" + out;
}

// The characters from U+3000 up (kana, kanji, full-width forms) before the
// first bracket, the ideographic space dropped:
// "しせんしょう  Ver 1.5 (19xx)" -> "j:しせんしょう"
std::string mdb_japanese_key(const char *name)
{
	std::string out;
	const unsigned char *p = (const unsigned char *)name;
	while (*p && *p != '(' && *p != '[')
	{
		int n = (*p < 0xC0) ? 1 : (*p < 0xE0) ? 2 : (*p < 0xF0) ? 3 : 4;
		int len = 0;
		while (len < n && p[len]) len++;
		if (*p >= 0xE3 && !(len == 3 && p[1] == 0x80 && p[2] == 0x80 && p[0] == 0xE3)) out.append((const char *)p, len);
		p += len;
	}
	return out.empty() ? out : "j:" + out;
}

// ---------------------------------------------------------------------------
// The file: tab separated records.
//   D <ram> <speed> <pad1> <pad2> <machine> <fdd>     what "-" resolves to
//   Y <year> <speed>      a title with no speed of its own, from this year on
//   T <id> <ram> <speed> <pad1> <pad2> <machine> <fdd> <boot> <save> <title> [<year>]
//   K <key> <id>
// The user's file is read after the shipped one; its rows replace by id or key.

enum { F_RAM, F_SPEED, F_PAD1, F_PAD2, F_MACHINE, F_FDD, F_BOOT, F_SAVE, F_COUNT };
static const uint8_t UNSET = 0xFF;

struct title_rec
{
	uint8_t f[F_COUNT];
	uint16_t year;
	std::string name;
};

static std::vector<title_rec> titles;
static std::unordered_map<std::string, int> title_of_id;
static std::unordered_map<std::string, std::string> id_of_key;
static uint8_t defaults[F_BOOT];
static int date_year;             // 0: no date rule
static uint8_t date_speed;
static time_t loaded_mtime[2];
static int loaded = 0;

// a field's text as the OSD value it selects; "-" is UNSET, junk is -1
static int field_value(int f, const char *v)
{
	static const char *words[F_COUNT][8] = {
		{ "2M", "4M", "6M", "8M" },
		{ "orig", "plus", "gs" },
		{ "pad", "6btn", "mouse", "stick", "apad", "none", "capcom", "towns" },
		{ "pad", "6btn", "mouse", "stick", "apad", "none", "capcom", "towns" },
		{ "towns", "marty" },
		{ "1", "2" },
		{ "-", "y" },
		{ "-", "y" } };

	if (f < F_BOOT && !strcmp(v, "-")) return UNSET;
	for (int i = 0; i < 8 && words[f][i]; i++) if (!strcmp(v, words[f][i])) return i;
	return -1;
}

// a line or field starting with # is a comment to the end of the line
static int parse_year(const char *s)
{
	int y = atoi(s);
	return (strlen(s) == 4 && y >= 1980 && y <= 2099) ? y : 0;
}

static int split(char *line, char **tok, int max)
{
	int n = 0;
	for (char *p = line; n < max && *p != '#';)
	{
		char *tab = strchr(p, '\t');
		if (tab) *tab = 0;
		char *e = p + strlen(p);
		while (e > p && (e[-1] == '\n' || e[-1] == '\r' || e[-1] == ' ')) *--e = 0;
		tok[n++] = p;
		if (!tab) break;
		p = tab + 1;
	}
	while (n && !tok[n - 1][0]) n--;
	return n;
}

static void load_file(const char *path)
{
	FILE *f = fopen(path, "r");
	if (!f) return;

	char line[4096];
	char *tok[12];
	int bad = 0;
	while (fgets(line, sizeof(line), f))
	{
		int n = split(line, tok, 12);
		if (!n) continue;

		if (!strcmp(tok[0], "D") && n == 7)
		{
			for (int i = 0; i < F_BOOT; i++)
			{
				int v = field_value(i, tok[1 + i]);
				if (v >= 0 && v != UNSET) defaults[i] = (uint8_t)v;
				else bad++;
			}
		}
		else if (!strcmp(tok[0], "Y") && n == 3)
		{
			int v = field_value(F_SPEED, tok[2]);
			if (parse_year(tok[1]) && v >= 0 && v != UNSET) { date_year = parse_year(tok[1]); date_speed = (uint8_t)v; }
			else bad++;
		}
		else if (!strcmp(tok[0], "T") && (n == 11 || n == 12))
		{
			title_rec t;
			t.name = tok[10];
			t.year = (n == 12) ? (uint16_t)parse_year(tok[11]) : 0;
			for (int i = 0; i < F_COUNT; i++)
			{
				int v = field_value(i, tok[2 + i]);
				if (v < 0) { bad++; v = (i < F_BOOT) ? UNSET : 0; }
				t.f[i] = (uint8_t)v;
			}
			auto it = title_of_id.find(tok[1]);
			if (it != title_of_id.end()) titles[it->second] = t;
			else
			{
				title_of_id[tok[1]] = (int)titles.size();
				titles.push_back(t);
			}
		}
		else if (!strcmp(tok[0], "K") && n == 3)
		{
			id_of_key[tok[1]] = tok[2];
		}
		else if (strcmp(tok[0], "V")) bad++;
	}
	fclose(f);
	if (bad) printf("Marty: %d bad entries in %s\n", bad, path);
}

static time_t mtime_of(const char *path)
{
	struct stat st;
	return (path && !stat(path, &st)) ? st.st_mtime : 0;
}

int mdb_load(const char *path, const char *user_path)
{
	time_t m0 = mtime_of(path), m1 = mtime_of(user_path);
	if (loaded && m0 == loaded_mtime[0] && m1 == loaded_mtime[1]) return (int)titles.size();

	titles.clear();
	title_of_id.clear();
	id_of_key.clear();
	static const uint8_t stock[F_BOOT] = { 0, 1, 0, 0, 0, 0 };   // 2 MB, Plus, pads, FM Towns, one drive
	memcpy(defaults, stock, sizeof(defaults));
	date_year = 0;
	load_file(path);
	if (user_path) load_file(user_path);

	loaded_mtime[0] = m0;
	loaded_mtime[1] = m1;
	loaded = 1;
	printf("Marty: settings database, %d titles, %d keys\n", (int)titles.size(), (int)id_of_key.size());
	return (int)titles.size();
}

static void to_settings(const uint8_t *f, uint16_t year, mdb_settings *s)
{
	uint8_t r[F_COUNT];
	for (int i = 0; i < F_COUNT; i++) r[i] = (i < F_BOOT && f[i] == UNSET) ? defaults[i] : f[i];
	s->ram = r[F_RAM];
	s->speed = r[F_SPEED];
	s->pad1 = r[F_PAD1];
	s->pad2 = r[F_PAD2];
	s->machine = r[F_MACHINE];
	s->fdd = r[F_FDD];
	s->boot = r[F_BOOT];
	s->save = r[F_SAVE];
	s->speed_given = f[F_SPEED] != UNSET;
	s->year = year;
}

void mdb_defaults(mdb_settings *s)
{
	uint8_t f[F_COUNT];
	memset(f, UNSET, F_BOOT);
	f[F_BOOT] = f[F_SAVE] = 0;
	to_settings(f, 0, s);
}

int mdb_date_speed(int year, uint8_t *speed)
{
	if (!date_year || !year || year < date_year) return 0;
	*speed = date_speed;
	return 1;
}

int mdb_find(const std::string &key, mdb_settings *s, std::string *title)
{
	if (key.empty()) return 0;
	auto k = id_of_key.find(key);
	if (k == id_of_key.end()) return 0;
	auto t = title_of_id.find(k->second);
	if (t == title_of_id.end()) return 0;
	to_settings(titles[t->second].f, titles[t->second].year, s);
	if (title) *title = titles[t->second].name;
	return 1;
}
