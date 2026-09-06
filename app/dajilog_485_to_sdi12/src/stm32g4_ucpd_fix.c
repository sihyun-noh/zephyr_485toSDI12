/*
 * Copyright (c) 2026
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/init.h>

#include <soc.h>

#if defined(CONFIG_SOC_SERIES_STM32G4X) && defined(PWR_CR3_UCPD_DBDIS)
#include <stm32_ll_bus.h>
#include <stm32_ll_pwr.h>
#endif

static int bridge_disable_stm32g4_ucpd_dead_battery(void)
{
#if defined(CONFIG_SOC_SERIES_STM32G4X) && defined(PWR_CR3_UCPD_DBDIS)
	if (IS_ENABLED(CONFIG_BRIDGE_DISABLE_STM32G4_UCPD_DEAD_BATTERY)) {
		LL_APB1_GRP1_EnableClock(LL_APB1_GRP1_PERIPH_PWR);
		LL_PWR_DisableUCPDDeadBattery();
	}
#endif

	return 0;
}

SYS_INIT(bridge_disable_stm32g4_ucpd_dead_battery, PRE_KERNEL_1, 1);
