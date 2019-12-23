/**************************************************************

   util.cpp - Utility functions

   ---------------------------------------------------------

   SwitchRes   Modeline generation engine for emulation

   GroovyMAME  Integration of SwitchRes into the MAME project
               Some reworked patches from SailorSat's CabMAME

   License     GPL-2.0+
   Copyright   2010-2016 - Chris Kennedy, Antonio Giner

 **************************************************************/

//============================================================
//  normalize
//============================================================

int normalize(int a, int b)
{
	int c, d;
	c = a % b;
	d = a / b;
	if (c) d++;
	return d * b;
}

//============================================================
//  real_res
//============================================================

int real_res(int x) {return (int) (x / 8) * 8;}
