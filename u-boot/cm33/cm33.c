#include <common.h>
#include <command.h>
#include <linux/delay.h>

DECLARE_GLOBAL_DATA_PTR;

#define CPG_SIPLL3_MON	(*(volatile u32 *)0x1101013C)	// PLL3 (SSCG) Monitor Register
#define CPG_CLKON_CM33	(*(volatile u32 *)0x11010504)	// Clock Control Register Cortex-M33
#define CPG_CLKMON_CM33	(*(volatile u32 *)0x11010684)	// Clock Monitor Register Cortex-M33
#define CPG_RST_CM33	(*(volatile u32 *)0x11010804)	// Reset Control Register Cortex-M33
#define CPG_RSTMON_CM33	(*(volatile u32 *)0x11010984)	// Reset Monitor Register Cortex-M33

#define SYS_CM33_CFG0	(*(volatile u32 *)0x11020804)	// CM33 Config Register0
#define SYS_CM33_CFG1	(*(volatile u32 *)0x11020808)	// CM33 Config Register1
#define SYS_CM33_CFG2	(*(volatile u32 *)0x1102080C)	// CM33 Config Register2
#define SYS_CM33_CFG3	(*(volatile u32 *)0x11020810)	// CM33 Config Register3
#define SYS_CM33_CTL	(*(volatile u32 *)0x11020818)	// CM33 Control Register
#define SYS_LSI_MODE	(*(volatile u32 *)0x11020A00)	// LSI Mode Signal Register
#define SYS_LP_CM33CTL1	(*(volatile u32 *)0x11020D28)	// Lowpower Sequence CM33 Control Register1

static void cm33_boot_normal_mode()
{
	// Supply clock to CM33_CLKIN
	CPG_CLKON_CM33 = 0x00010001;

	// Poll CPG_CLKMON_CM33 to confirm that CM33_CLKIN clock is supplied
	while (CPG_CLKMON_CM33 != 0x1)
		mdelay(10);

	// Stop the reset signals (released from the reset state)
	CPG_RST_CM33 = 0x00070007;

	// Poll CPG_RSTMON_CM33 to confirm that all the reset signals are not applied
	while(CPG_RSTMON_CM33 != 0)
		mdelay(10);
}

static void cm33_boot_debug_mode()
{
	// Supply clock to CM33_TSCLK and CM33_CLKIN
	CPG_CLKON_CM33 = 0x00030003;

	// Poll CPG_CLKMON_CM33 to confirm that both CM33_TSCLK and CM33_CLKIN clock are supplied
	while (CPG_CLKMON_CM33 != 0x3)
		mdelay(10);

	// Set DEBUGQREQn bit of SYS_LP_CM33CTL1 to 1
	SYS_LP_CM33CTL1 = 0x00001100;

	// Poll SYS_LP_CM33CTL1 to check if DEBUGQACCEPTn bit becomes 1
	// Fixme. Lacking of SYS_LP_CM33CTL1.DEBUGQACCEPTn info

	// Set FETCHCNT bit of SYS_CM33_CTL register to 1
	SYS_CM33_CTL = 0x00000001;

	// Stop the reset signals (released from the reset state)
	CPG_RST_CM33 = 0x00070007;

	// Poll CPG_RSTMON_CM33 to confirm that all the reset signals are not applied
	while (CPG_RSTMON_CM33 != 0)
		mdelay(10);

	// Set FETCHCNT bit of SYS_CM33_CTL register to 0
	SYS_CM33_CTL = 0x00000000;
}

void cm33_start(u8 debug, u32 s_addr, u32 ns_addr)
{
	// Check if the SSCG PLL3 is ON or not
	if ((CPG_SIPLL3_MON & 0x1) == 0x1) {
		SYS_CM33_CFG0 = 0x00103CE5;
		SYS_CM33_CFG1 = 0x00103CE5;
	} else {
		SYS_CM33_CFG0 = 0x00003D08;
		SYS_CM33_CFG1 = 0x00003D08;
	}

	// Set the secure vector address of Cortex-M33
	SYS_CM33_CFG2 = s_addr;

	// Set the non secure vector address of Cortex-M33
	SYS_CM33_CFG3 = ns_addr;

	// Start the CM33 propram in normal/debug mode
	debug ? cm33_boot_debug_mode() : cm33_boot_normal_mode();
}

int do_cm33(struct cmd_tbl *cmdtp, int flag, int argc, char * const argv[])
{
	u32 s_addr, ns_addr;

	/* need at least two arguments */
	if (argc < 2)
		goto usage;

	if (strcmp(argv[1], "start_normal") == 0) {
		/* two addresses are required */
		if (argc < 4)
			goto usage;

		s_addr = simple_strtoul(argv[2], NULL, 16);
		ns_addr = simple_strtoul(argv[3], NULL, 16);
		cm33_start(0, s_addr, ns_addr);
	} else if (strcmp(argv[1], "start_debug") == 0) {
		/* two addresses are required */
		if (argc < 4)
			goto usage;

		s_addr = simple_strtoul(argv[2], NULL, 16);
		ns_addr = simple_strtoul(argv[3], NULL, 16);
		cm33_start(1, s_addr, ns_addr);
	} else
		goto usage;

	return 0;

usage:
	return CMD_RET_USAGE;
}

static char cm33_help_text[] =
	"\n\tstart_normal [s_addr] [ns_addr] - Reset and start CM33 in normal mode with firmware\n"
	"\t\t\tlocated at address [s_addr] (secure) and [ns_addr] (non secure)\n"
	"\tstart_debug [s_addr] [ns_addr] - Reset and start CM33 in debug mode with firmware\n"
	"\t\t\tlocated at address [s_addr] (secure) and [ns_addr] (non secure)\n"
	"";

U_BOOT_CMD(
	cm33, CONFIG_SYS_MAXARGS, 1, do_cm33,
	"Control CM33 CPU", cm33_help_text
);
