#ifndef MAT4x4_H
#define MAT4x4_H

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct mat4x4
{
	union
	{
		struct
		{
			float
			m11, m12, m13, m14,
			m21, m22, m23, m24,
			m31, m32, m33, m34,
			m41, m42, m43, m44;
		};

		float comp[16];
		float comp_2d[4][4];
	};

	mat4x4(void)
	{
		memset(comp, 0, 16*sizeof(float));
	}

	mat4x4(const float mat[16])
	: mat4x4()
	{
		for ( size_t i = 0; i < 16; i++ )
		{
			comp[i] = mat[i];
		}
	}

	void setIdentity()
	{
		m11 = m22 = m33 = m44 = 1.0f;
	}

	mat4x4 operator* (const mat4x4 b)
	{
		mat4x4 a = *this;
		for( size_t r = 0; r < 4; r++ )
		{
			for( size_t c = 0; c < 4; c++ )
			{
				comp_2d[r][c] =
				b.comp_2d[r][0] * a.comp_2d[0][c] +
				b.comp_2d[r][1] * a.comp_2d[1][c] +
				b.comp_2d[r][2] * a.comp_2d[2][c] +
				b.comp_2d[r][3] * a.comp_2d[3][c];
			}
		}

		return *this;
	}

	// if the matrix has values over x, compress the rest down to make sure it fits
	void compress(float x)
	{
		float maximum = 0.0;
		bool max_found = false;

		// find maximum
		for ( size_t i = 0; i < 16; i++)
		{
			float& y = comp[i];

			if (y > x)
			{
				maximum = abs(y) > maximum ? abs(y) : maximum;
				max_found = true;
			}
		}

		// apply maximum, range will be [-x .. x]
		if (max_found)
		{
			for ( size_t i = 0; i < 16; i++)
			{
				comp[i] /= maximum;
				comp[i] *= x;
			}
		}
	}
};

#endif
