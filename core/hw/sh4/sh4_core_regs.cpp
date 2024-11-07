/*
	Sh4 register storage/functions/utilities
*/

#include "types.h"
#include "sh4_core.h"
#include "sh4_interrupts.h"
#if defined(__ANDROID__) && HOST_CPU == CPU_ARM
#include <fenv.h>
#endif

Sh4RCB* p_sh4rcb;

static void ChangeGPR()
{
	std::swap((u32 (&)[8])r, r_bank);
}

static void ChangeFP()
{
	std::swap((f32 (&)[16])Sh4cntx.xffr, *(f32 (*)[16])&Sh4cntx.xffr[16]);
}

//called when sr is changed and we must check for reg banks etc.
//returns true if interrupt pending
bool UpdateSR()
{
	if (sr.MD)
	{
		if (old_sr.RB != sr.RB)
			ChangeGPR();//bank change
	}
	else
	{
		if (old_sr.RB)
			ChangeGPR();//switch
	}

	old_sr.status = sr.status;
	old_sr.RB &= sr.MD;

	return SRdecode();
}

// make host and sh4 rounding and denormal modes match
static u32 old_rm = 0xFF;
static u32 old_dn = 0xFF;

static void setHostRoundingMode()
{
	if (old_rm != fpscr.RM || old_dn != fpscr.DN)
	{
		old_rm = fpscr.RM;
		old_dn = fpscr.DN;
        
        //Correct rounding is required by some games (SOTB, etc)
#ifdef _MSC_VER
        if (fpscr.RM == 1)  //if round to 0 , set the flag
            _controlfp(_RC_CHOP, _MCW_RC);
        else
            _controlfp(_RC_NEAR, _MCW_RC);
        
        if (fpscr.DN)     //denormals are considered 0
            _controlfp(_DN_FLUSH, _MCW_DN);
        else
            _controlfp(_DN_SAVE, _MCW_DN);
#else

    #if HOST_CPU==CPU_X86 || HOST_CPU==CPU_X64

            u32 temp=0x1f80;	//no flush to zero && round to nearest

			if (fpscr.RM==1)  //if round to 0 , set the flag
				temp|=(3<<13);

			if (fpscr.DN)     //denormals are considered 0
				temp|=(1<<15);
			asm("ldmxcsr %0" : : "m"(temp));
    #elif HOST_CPU==CPU_ARM
		static const unsigned int offMask = 0x04086060;
		unsigned int onMask = 0x02000000;

		if (fpscr.RM == 1)  //if round to 0 , set the flag
			onMask |= 3 << 22;

		if (fpscr.DN)
			onMask |= 1 << 24;

		#ifdef __ANDROID__
			fenv_t fenv;
			fegetenv(&fenv);
			fenv &= offMask;
			fenv |= onMask;
			fesetenv(&fenv);
		#else
			int raa;
	
			asm volatile
				(
					"fmrx   %0, fpscr   \n\t"
					"and    %0, %0, %1  \n\t"
					"orr    %0, %0, %2  \n\t"
					"fmxr   fpscr, %0   \n\t"
					: "=r"(raa)
					: "r"(offMask), "r"(onMask)
				);
		#endif
	#elif HOST_CPU == CPU_ARM64
		static const unsigned long off_mask = 0x04080000;
        unsigned long on_mask = 0x02000000;    // DN=1 Any operation involving one or more NaNs returns the Default NaN

        if (fpscr.RM == 1)		// if round to 0, set the flag
        	on_mask |= 3 << 22;

        if (fpscr.DN)
        	on_mask |= 1 << 24;	// flush denormalized numbers to zero

        asm volatile
            (
                "MRS    x10, FPCR     \n\t"
                "AND    x10, x10, %0  \n\t"
                "ORR    x10, x10, %1  \n\t"
                "MSR    FPCR, x10     \n\t"
                :
                : "r"(off_mask), "r"(on_mask)
				: "x10"
            );
    #else
	#error "SetFloatStatusReg: Unsupported platform"
    #endif
#endif

	}
}

//called when fpscr is changed and we must check for reg banks etc..
void UpdateFPSCR()
{
	if (fpscr.FR !=old_fpscr.FR)
		ChangeFP(); // FPU bank change

	old_fpscr=fpscr;
	setHostRoundingMode();
}

void RestoreHostRoundingMode()
{
	old_rm = 0xFF;
	old_dn = 0xFF;
	setHostRoundingMode();
}

void setDefaultRoundingMode()
{
	u32 savedRM = fpscr.RM;
	u32 savedDN = fpscr.DN;
	fpscr.RM = 0;
	fpscr.DN = 0;
	setHostRoundingMode();
	fpscr.RM = savedRM;
	fpscr.DN = savedDN;
}
