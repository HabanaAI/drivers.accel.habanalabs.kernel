/* SPDX-License-Identifier: GPL-2.0
 *
 * Copyright 2016-2021 HabanaLabs, Ltd.
 * All Rights Reserved.
 *
 */

/************************************
 ** This is an auto-generated file **
 **       DO NOT EDIT BELOW        **
 ************************************/

#ifndef ASIC_REG_MMU_REGS_H_
#define ASIC_REG_MMU_REGS_H_

/*
 *****************************************
 *   MMU
 *   (Prototype: MMU)
 *****************************************
 */

#define mmMMU_TRACE_CTRL 0x0

#define mmMMU_PAGE_ERROR_CAPTURE 0x10

#define mmMMU_PAGE_ERROR_CAPTURE_VA 0x14

#define mmMMU_ACCESS_ERROR_CAPTURE 0x18

#define mmMMU_ACCESS_ERROR_CAPTURE_VA 0x1C

#define mmMMU_ACCESS_PAGE_ERROR_VALID 0x20

#define mmMMU_MMU_BYPASS 0x30

#define mmMMU_STATIC_MULTI_PAGE_SIZE 0x34

#define mmMMU_PAGE_FAULT_ID_LSB 0x38

#define mmMMU_PAGE_FAULT_ID_MSB 0x3C

#define mmMMU_PAGE_ACCESS_ID_LSB 0x40

#define mmMMU_PAGE_ACCESS_ID_MSB 0x44

#define mmMMU_RAZWI_WRITE_VLD 0x58

#define mmMMU_RAZWI_WRITE_ID_31_0 0x5C

#define mmMMU_RAZWI_WRITE_ID_63_32 0x60

#define mmMMU_RAZWI_READ_VLD 0x64

#define mmMMU_RAZWI_READ_ID_31_0 0x68

#define mmMMU_RAZWI_READ_ID_63_32 0x6C

#define mmMMU_RAZWI_ADDR_LSB 0x74

#define mmMMU_RAZWI_ADDR_MSB 0x78

#define mmMMU_INV_START_RANGE_LO 0x7C

#define mmMMU_INV_START_RANGE_HI 0x80

#define mmMMU_INV_END_RANGE_LO 0x84

#define mmMMU_INV_END_RANGE_HI 0x88

#define mmMMU_INV_ASID 0x8C

#define mmMMU_INV_ASID_EN 0x90

#define mmMMU_INV_RANGE_TRIGGER 0x94

#define mmMMU_INV_ALL_START 0x98

#define mmMMU_INV_CPL_ADDR 0x9C

#define mmMMU_INV_CPL_DATA 0xA0

#define mmMMU_FAULTQ_BASE_ADDR_LO 0xA4

#define mmMMU_FAULTQ_BASE_ADDR_HI 0xA8

#define mmMMU_FAULTQ_PI 0xAC

#define mmMMU_FAULTQ_CI 0xB0

#define mmMMU_FAULTQ_SIZE 0xB4

#define mmMMU_FAULTQ_MAX_THSHLD 0xB8

#define mmMMU_FAULTQ_MAX_THSLD_INTR_ADD 0xBC

#define mmMMU_FAULTQ_MAX_THSLD_INTR_DAT 0xC0

#define mmMMU_FAULTQ_MAX_THSLD_INT_MASK 0xC4

#define mmMMU_FAULTQ_STATUS 0xC8

#define mmMMU_FAULTQ_MSIX_ADDR 0xCC

#define mmMMU_FAULTQ_MSIX_DATA 0xD0

#define mmMMU_PTW_RESUME 0xD4

#define mmMMU_SET_SCRAMBLE_REG_0 0xD8

#define mmMMU_SET_SCRAMBLE_REG_1 0xDC

#define mmMMU_SET_SCRAMBLE_REG_2 0xE0

#define mmMMU_SET_SCRAMBLE_REG_3 0xE4

#define mmMMU_SET_SCRAMBLE_REG_4 0xE8

#define mmMMU_FEATURE_CONTROL 0xEC

#define mmMMU_PAGE_FAULT_USER_BITS 0xF0

#define mmMMU_FAULTQ_AW_CACHE 0xF4

#define mmMMU_FAULTQ_AW_PROT 0xF8

#define mmMMU_FAULTQ_AW_LOCK 0xFC

#define mmMMU_FAULTQ_MSIX_AW_LOCK 0x100

#define mmMMU_FAULTQ_MSIX_AW_CACHE 0x104

#define mmMMU_FAULTQ_MSIX_AW_PROT 0x108

#define mmMMU_DRAIN_TIMEOUT_VAL_HI 0x10C

#define mmMMU_DRAIN_CTRL 0x110

#define mmMMU_PREFETCH_USER_IDX 0x114

#define mmMMU_DRAIN_TIMEOUT_VAL_LO 0x118

#define mmMMU_PLRU_MODE_SEL 0x11C

#define mmMMU_SPI_MASK 0x120

#define mmMMU_SPI_STATUS 0x124

#define mmMMU_SEI_MASK 0x128

#define mmMMU_SEI_STATUS 0x12C

#define mmMMU_REI_MASK 0x130

#define mmMMU_REI_STATUS 0x134

#define mmMMU_DBG_REG_0 0x138

#endif /* ASIC_REG_MMU_REGS_H_ */
