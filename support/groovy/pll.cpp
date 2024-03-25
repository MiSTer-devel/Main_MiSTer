#include <math.h>
#include <time.h>
#include <stdint.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <linux/types.h>

#include "pll.h"

#define REF_CLOCK       50    //reference clock
#define MAX_CLOCK     1600    //max M clock
#define MIN_N            1    //min divide factor
#define MAX_N            8    //max divide factor (can be superior at cost for cpu)

//from video.cpp
static int findPLLpar(double Fout, uint32_t *pc, uint32_t *pm, double *pko)
{
	uint32_t c = 1;
	while ((Fout*c) < 400) c++;

	while (1)
	{
		double fvco = Fout*c;
		uint32_t m = (uint32_t)(fvco / 50);
		double ko = ((fvco / 50) - m);

		fvco = ko + m;
		fvco *= 50.f;

		if (ko && (ko <= 0.05f || ko >= 0.95f))
		{
			//printf("Fvco=%f, C=%d, M=%d, K=%f ", fvco, c, m, ko);
			if (fvco > 1500.f)
			{
				printf("-> No exact parameters found\n");
				return 0;
			}
			//printf("-> K is outside allowed range\n");
			c++;
		}
		else
		{
			*pc = c;
			*pm = m;
			*pko = ko;
			return 1;
		}
	}

	//will never reach here
	return 0;
}

//from video.cpp
double getMCK_PLL_Fractional(double const Fout, int &M, int &C, int &K)
{
  	double Fpix;
	double fvco, ko;
	uint32_t m, c;

	//printf("Calculate PLL for %.4f MHz:\n", Fout);

	if (!findPLLpar(Fout, &c, &m, &ko))
	{
		c = 1;
		while ((Fout*c) < 400) c++;

		fvco = Fout*c;
		m = (uint32_t)(fvco / 50);
		ko = ((fvco / 50) - m);

		//Make sure K is in allowed range.
		if (ko <= 0.05f)
		{
			ko = 0;
		}
		else if (ko >= 0.95f)
		{
			m++;
			ko = 0;
		}
	}

	uint32_t k = ko ? (uint32_t)(ko * 4294967296) : 1;

	fvco = ko + m;
	fvco *= 50.f;
	Fpix = fvco / c;

	//printf("Fvco=%f, C=%d, M=%d, K=%f(%u) -> Fpix=%f\n", fvco, c, m, ko, k, Fpix);		
	
	M = m;
	C = c;
	K = k;	
	
	return Fpix;
}

//based on Quartus
int getMinM(int N) 
{
	switch(N) // + 6
	{
		case 1: return 7;
		case 2: return 13;
		case 3: return 19;
		case 4: return 25;
		case 5: return 32;
		case 6: return 38;
		case 7: return 44;
		case 8: return 50;
	}
	return 0;	
}

//based on Quartus
int getMaxM(int N) 
{
	return N << 5 ;	//mult x32
}

//based on Quartus (not validate min or max here)
int isValidM(int M, int N) 
{
	if (M % 2 == 0) return 1;
	else
	{
		switch(N)
		{
			case 1: return 1;
			case 2: return 1;
			case 3: return 1;
			case 4: return 1;
			case 5: 
			{
				if (M == 45 || M == 55 || M >= 61) return 1;
				else return 0;
			};
			case 6: 
			{
				if (M == 45 || M == 57 || M == 63 || M >= 73) return 1;
				else return 0;
			};
			case 7: 
			{
				if (M == 63 || M == 77 || M >= 85) return 1;
				else return 0;
			};
			case 8: 
			{
				if (M >= 97) return 1;
				else return 0;
			};
		}	
	}
	return 0;	
}

double getMinClock(int N)
{
	return REF_CLOCK / (double) N;	
}

int getMinC(double const px, int N) 
{
	return ceil(getMinClock(N) / px);	
}

int getMaxC(double const px) 
{	
	return floor(MAX_CLOCK / px);	
}

double getMNC_PLL_Integer(double const px, int &M, int &N, int &C)
{						
	double error = 9999.9999;
	M = 0;
	N = 0;
	C = 0;	
	
	for (int n = MIN_N; n <= MAX_N; n++)	//Quartus based values
	{
		int c_min = getMinC(px, n);	
		int c_max = getMaxC(px);		
		for (int c = c_min; c <= c_max; c++)
		{
			double clock = c * px;
			double val = clock / REF_CLOCK;
			double tmp_error = error;			
			double tmp_val;
			int m_min = getMinM(n);
			int m_max = getMaxM(n);	
			
			double tmp_m_error = 9999.9999;
			bool stopM = false;									
			for (int m = m_min; m <= m_max && !stopM; m++) 
			{				
				if (isValidM(m, n))
				{
					tmp_val = m / (double) n;	
					tmp_error = fabs(val - tmp_val);					
					if (tmp_error <= error) 
			 		{    
						error = tmp_error;
			 			M = m;
			 			N = n;  
			 			C = c;				 			
			 		};
			 		if (tmp_error > tmp_m_error)
			 		{
			 			stopM = true;
			 		}			 		
			 		tmp_m_error = tmp_error;		
			 		 
				}							 					 		
			}	
		}		
		
	}		
	return error;
}

