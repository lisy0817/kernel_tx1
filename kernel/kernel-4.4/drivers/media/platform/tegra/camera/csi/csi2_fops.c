/*
 * Tegra CSI2 device common APIs
 *
 * Copyright (c) 2016-2017, NVIDIA CORPORATION.  All rights reserved.
 *
 * Author: Bryan Wu <pengw@nvidia.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/device.h>
#include <linux/clk/tegra.h>
#include "camera/csi/csi.h"
#include "camera/vi/mc_common.h"
#include "mipical/mipi_cal.h"
#include "nvhost_acm.h"
#include <linux/clk/tegra.h>

static void csi_write(struct tegra_csi_channel *chan, unsigned int addr,
			u32 val, u8 port)
{
	struct tegra_csi_device *csi = chan->csi;

	writel(val, (csi->iomem[port] + addr));
}

static u32 csi_read(struct tegra_csi_channel *chan, unsigned int addr,
			u8 port)
{
	struct tegra_csi_device *csi = chan->csi;

	return readl((csi->iomem[port] + addr));
}

/* Pixel parser registers accessors */
static void pp_write(struct tegra_csi_port *port, u32 addr, u32 val)
{
	writel(val, port->pixel_parser + addr);
}

static u32 pp_read(struct tegra_csi_port *port, u32 addr)
{
	return readl(port->pixel_parser + addr);
}

/* CSI CIL registers accessors */
static void cil_write(struct tegra_csi_port *port, u32 addr, u32 val)
{
	writel(val, port->cil + addr);
}

static u32 cil_read(struct tegra_csi_port *port, u32 addr)
{
	return readl(port->cil + addr);
}

/* Test pattern generator registers accessor */
static void tpg_write(struct tegra_csi_port *port,
			unsigned int addr, u32 val)
{
	writel(val, port->tpg + addr);
}

int tegra_csi_error(struct tegra_csi_channel *chan,
			enum tegra_csi_port_num port_num)
{
	struct tegra_csi_port *port;
	u32 val;
	int err = 0;

	port = &chan->ports[port_num];
	/*
	 * only uncorrectable header error and multi-bit
	 * transmission errors are checked as they cannot be
	 * corrected automatically
	 */
	val = pp_read(port, TEGRA_CSI_PIXEL_PARSER_STATUS);
	err |= val & 0x4000;
	pp_write(port, TEGRA_CSI_PIXEL_PARSER_STATUS, val);

	val = cil_read(port, TEGRA_CSI_CIL_STATUS);
	err |= val & 0x02;
	cil_write(port, TEGRA_CSI_CIL_STATUS, val);

	val = cil_read(port, TEGRA_CSI_CILX_STATUS);
	err |= val & 0x00020020;
	cil_write(port, TEGRA_CSI_CILX_STATUS, val);

	return err;
}

void tegra_csi_status(struct tegra_csi_channel *chan,
			enum tegra_csi_port_num port_num)
{
	int i;
	u32 val;
	struct tegra_csi_port *port;

	for (i = 0; i < chan->numports; i++) {
		port = &chan->ports[i];
		val = pp_read(port, TEGRA_CSI_PIXEL_PARSER_STATUS);

		dev_dbg(chan->csi->dev,
			"TEGRA_CSI_PIXEL_PARSER_STATUS 0x%08x\n",
			val);

		val = cil_read(port, TEGRA_CSI_CIL_STATUS);
		dev_dbg(chan->csi->dev,
			"TEGRA_CSI_CIL_STATUS 0x%08x\n", val);

		val = cil_read(port, TEGRA_CSI_CILX_STATUS);
		dev_dbg(chan->csi->dev,
			"TEGRA_CSI_CILX_STATUS 0x%08x\n", val);
	}
}
EXPORT_SYMBOL(tegra_csi_status);

void tegra_csi_error_recover(struct tegra_csi_channel *chan,
				enum tegra_csi_port_num port_num)
{
	struct tegra_csi_port *port;
	struct tegra_csi_device *csi;
	int i;

	csi = chan->csi;

	for (i = 0; i < chan->numports; i++) {
		port = &chan->ports[i];

		if (port->lanes == 4) {
			int port_val = ((port_num >> 1) << 1);
			struct tegra_csi_port *port_a =
				&chan->ports[port_val];
			struct tegra_csi_port *port_b =
				&chan->ports[port_val+1];

			tpg_write(port_a,
				TEGRA_CSI_PATTERN_GENERATOR_CTRL, PG_ENABLE);
			tpg_write(port_b,
				TEGRA_CSI_PATTERN_GENERATOR_CTRL, PG_ENABLE);
			cil_write(port_a,
				TEGRA_CSI_CIL_SW_SENSOR_RESET, 0x1);
			cil_write(port_b,
				TEGRA_CSI_CIL_SW_SENSOR_RESET, 0x1);
			csi_write(chan, TEGRA_CSI_CSI_SW_STATUS_RESET, 0x1,
					port_num >> 1);
			/* sleep for clock cycles to drain the Rx FIFO */
			usleep_range(10, 20);
			cil_write(port_a,
				TEGRA_CSI_CIL_SW_SENSOR_RESET, 0x0);
			cil_write(port_b,
				TEGRA_CSI_CIL_SW_SENSOR_RESET, 0x0);
			csi_write(chan,
				TEGRA_CSI_CSI_SW_STATUS_RESET,
				0x0, port_num >> 1);
			tpg_write(port_a,
				TEGRA_CSI_PATTERN_GENERATOR_CTRL, PG_DISABLE);
			tpg_write(port_b,
				TEGRA_CSI_PATTERN_GENERATOR_CTRL, PG_DISABLE);
		} else {
			tpg_write(port,
				TEGRA_CSI_PATTERN_GENERATOR_CTRL, PG_ENABLE);
			cil_write(port,
				TEGRA_CSI_CIL_SW_SENSOR_RESET, 0x1);
			csi_write(chan,
				TEGRA_CSI_CSI_SW_STATUS_RESET,
				0x1, port_num >> 1);
			/* sleep for clock cycles to drain the Rx FIFO */
			usleep_range(10, 20);
			cil_write(port,
				TEGRA_CSI_CIL_SW_SENSOR_RESET, 0x0);
			csi_write(chan,
				TEGRA_CSI_CSI_SW_STATUS_RESET,
				0x0, port_num >> 1);
			tpg_write(port,
				TEGRA_CSI_PATTERN_GENERATOR_CTRL, PG_DISABLE);
		}
	}
}


static int tpg_clk_enable(struct tegra_csi_device *csi)
{
	int err = 0;

	mutex_lock(&csi->source_update);
	if (csi->tpg_active != 1) {
		mutex_unlock(&csi->source_update);
		return 0;
	}
	mutex_unlock(&csi->source_update);

	clk_set_rate(csi->plld, TEGRA_CLOCK_TPG);
	err = clk_prepare_enable(csi->plld);
	if (err) {
		dev_err(csi->dev, "pll_d enable failed");
		return err;
	}

	err = clk_prepare_enable(csi->plld_dsi);
	if (err) {
		dev_err(csi->dev, "pll_d enable failed");
		goto plld_dsi_err;
	}
	tegra210_csi_source_from_plld();
	return err;
plld_dsi_err:
	clk_disable_unprepare(csi->plld);
	return err;
}

static int tpg_clk_disable(struct tegra_csi_device *csi)
{
	int err = 0;

	mutex_lock(&csi->source_update);
	if (csi->tpg_active != 0) {
		mutex_unlock(&csi->source_update);
		return 0;
	}
	mutex_unlock(&csi->source_update);
	tegra210_csi_source_from_brick();
	clk_disable_unprepare(csi->plld_dsi);
	clk_disable_unprepare(csi->plld);

	return err;
}

static int csi2_tpg_start_streaming(struct tegra_csi_channel *chan,
			      enum tegra_csi_port_num port_num)
{
	struct tegra_csi_port *port = &chan->ports[port_num];

	tpg_write(port, TEGRA_CSI_PATTERN_GENERATOR_CTRL,
		       ((chan->pg_mode - 1) << PG_MODE_OFFSET) |
		       PG_ENABLE);
	tpg_write(port, TEGRA_CSI_PG_BLANK,
			port->v_blank << PG_VBLANK_OFFSET |
			port->h_blank);
	tpg_write(port, TEGRA_CSI_PG_PHASE, 0x0);
	tpg_write(port, TEGRA_CSI_PG_RED_FREQ,
		       (0x10 << PG_RED_VERT_INIT_FREQ_OFFSET) |
		       (0x10 << PG_RED_HOR_INIT_FREQ_OFFSET));
	tpg_write(port, TEGRA_CSI_PG_RED_FREQ_RATE, 0x0);
	tpg_write(port, TEGRA_CSI_PG_GREEN_FREQ,
		       (0x10 << PG_GREEN_VERT_INIT_FREQ_OFFSET) |
		       (0x10 << PG_GREEN_HOR_INIT_FREQ_OFFSET));
	tpg_write(port, TEGRA_CSI_PG_GREEN_FREQ_RATE, 0x0);
	tpg_write(port, TEGRA_CSI_PG_BLUE_FREQ,
		       (0x10 << PG_BLUE_VERT_INIT_FREQ_OFFSET) |
		       (0x10 << PG_BLUE_HOR_INIT_FREQ_OFFSET));
	tpg_write(port, TEGRA_CSI_PG_BLUE_FREQ_RATE, 0x0);
	return 0;
}

int csi2_start_streaming(struct tegra_csi_channel *chan,
				enum tegra_csi_port_num port_num)
{
	struct tegra_csi_port *port = &chan->ports[port_num];
	int csi_port, csi_lanes;

	csi_port = chan->ports[port_num].num;
	csi_lanes = chan->ports[port_num].lanes;

	csi_write(chan, TEGRA_CSI_CLKEN_OVERRIDE, 0, csi_port >> 1);

	/* Clean up status */
	pp_write(port, TEGRA_CSI_PIXEL_PARSER_STATUS, 0xFFFFFFFF);
	cil_write(port, TEGRA_CSI_CIL_STATUS, 0xFFFFFFFF);
	cil_write(port, TEGRA_CSI_CILX_STATUS, 0xFFFFFFFF);

	cil_write(port, TEGRA_CSI_CIL_INTERRUPT_MASK, 0x0);

	/* CIL PHY registers setup */
	cil_write(port, TEGRA_CSI_CIL_PAD_CONFIG0, 0x0);
	cil_write(port, TEGRA_CSI_CIL_PHY_CONTROL,
			BYPASS_LP_SEQ | 0xA);

	/*
	 * The CSI unit provides for connection of up to six cameras in
	 * the system and is organized as three identical instances of
	 * two MIPI support blocks, each with a separate 4-lane
	 * interface that can be configured as a single camera with 4
	 * lanes or as a dual camera with 2 lanes available for each
	 * camera.
	 */
	if (csi_lanes == 4) {
		unsigned int cilb_offset;

		cilb_offset = TEGRA_CSI_CIL_OFFSET + TEGRA_CSI_PORT_OFFSET;

		cil_write(port, TEGRA_CSI_CIL_PAD_CONFIG0,
				BRICK_CLOCK_A_4X);
		csi_write(chan, cilb_offset + TEGRA_CSI_CIL_PAD_CONFIG0, 0x0,
				csi_port >> 1);
		csi_write(chan, cilb_offset + TEGRA_CSI_CIL_INTERRUPT_MASK, 0x0,
				csi_port >> 1);
		cil_write(port, TEGRA_CSI_CIL_PHY_CONTROL,
				BYPASS_LP_SEQ | 0xA);
		csi_write(chan, cilb_offset + TEGRA_CSI_CIL_PHY_CONTROL,
				BYPASS_LP_SEQ | 0xA, csi_port >> 1);
		csi_write(chan, TEGRA_CSI_PHY_CIL_COMMAND,
				CSI_A_PHY_CIL_ENABLE | CSI_B_PHY_CIL_ENABLE,
				csi_port >> 1);
	} else {
		u32 val = csi_read(chan, TEGRA_CSI_PHY_CIL_COMMAND,
					csi_port >> 1);

		csi_write(chan,
			TEGRA_CSI_CIL_OFFSET + TEGRA_CSI_CIL_PAD_CONFIG0, 0x0,
			csi_port >> 1);
		val = ((csi_port & 0x1) == PORT_A) ?
			CSI_A_PHY_CIL_ENABLE | CSI_B_PHY_CIL_NOP
			: CSI_B_PHY_CIL_ENABLE | CSI_A_PHY_CIL_NOP;
		csi_write(chan, TEGRA_CSI_PHY_CIL_COMMAND, val,
				csi_port >> 1);
	}
	/* CSI pixel parser registers setup */
	pp_write(port, TEGRA_CSI_PIXEL_STREAM_PP_COMMAND,
			(0xF << CSI_PP_START_MARKER_FRAME_MAX_OFFSET) |
			CSI_PP_SINGLE_SHOT_ENABLE | CSI_PP_RST);
	pp_write(port, TEGRA_CSI_PIXEL_PARSER_INTERRUPT_MASK, 0x0);
	pp_write(port, TEGRA_CSI_PIXEL_STREAM_CONTROL0,
			CSI_PP_PACKET_HEADER_SENT |
			CSI_PP_DATA_IDENTIFIER_ENABLE |
			CSI_PP_WORD_COUNT_SELECT_HEADER |
			CSI_PP_CRC_CHECK_ENABLE |  CSI_PP_WC_CHECK |
			CSI_PP_OUTPUT_FORMAT_STORE | CSI_PPA_PAD_LINE_NOPAD |
			CSI_PP_HEADER_EC_DISABLE | CSI_PPA_PAD_FRAME_NOPAD |
			(csi_port & 1));
	pp_write(port, TEGRA_CSI_PIXEL_STREAM_CONTROL1,
			(0x1 << CSI_PP_TOP_FIELD_FRAME_OFFSET) |
			(0x1 << CSI_PP_TOP_FIELD_FRAME_MASK_OFFSET));
	pp_write(port, TEGRA_CSI_PIXEL_STREAM_GAP,
			0x14 << PP_FRAME_MIN_GAP_OFFSET);
	pp_write(port, TEGRA_CSI_PIXEL_STREAM_EXPECTED_FRAME, 0x0);
	pp_write(port, TEGRA_CSI_INPUT_STREAM_CONTROL,
			(0x3f << CSI_SKIP_PACKET_THRESHOLD_OFFSET) |
			(csi_lanes - 1));

	if (chan->pg_mode) {
		tpg_clk_enable(chan->csi);
		csi2_tpg_start_streaming(chan, port_num);
	}

	pp_write(port, TEGRA_CSI_PIXEL_STREAM_PP_COMMAND,
			(0xF << CSI_PP_START_MARKER_FRAME_MAX_OFFSET) |
			CSI_PP_SINGLE_SHOT_ENABLE | CSI_PP_ENABLE);
	return 0;
}

void csi2_stop_streaming(struct tegra_csi_channel *chan,
				enum tegra_csi_port_num port_num)
{
	struct tegra_csi_port *port = &chan->ports[port_num];


	if (chan->pg_mode) {
		tpg_write(port, TEGRA_CSI_PATTERN_GENERATOR_CTRL, PG_DISABLE);
		tpg_clk_disable(chan->csi);
	}
	if (!port) {
		pr_err("%s:no port\n", __func__);
		return;
	}
	pp_write(port, TEGRA_CSI_PIXEL_STREAM_PP_COMMAND,
			(0xF << CSI_PP_START_MARKER_FRAME_MAX_OFFSET) |
			CSI_PP_DISABLE);
}

int csi2_hw_init(struct tegra_csi_device *csi)
{
	int i, csi_port;
	struct tegra_csi_channel *it;
	struct tegra_csi_port *port;

	csi->iomem[0] = (csi->iomem_base + TEGRA_CSI_PIXEL_PARSER_0_BASE);
	csi->iomem[1] = (csi->iomem_base + TEGRA_CSI_PIXEL_PARSER_2_BASE);
	csi->iomem[2] = (csi->iomem_base + TEGRA_CSI_PIXEL_PARSER_4_BASE);
	list_for_each_entry(it, &csi->csi_chans, list) {
		for (i = 0; i < it->numports; i++) {
			port = &it->ports[i];
			csi_port = it->ports[i].num;
			port->pixel_parser = csi->iomem[csi_port >> 1] +
				(csi_port % 2) * TEGRA_CSI_PORT_OFFSET;
			port->cil = port->pixel_parser + TEGRA_CSI_CIL_OFFSET;
			port->tpg = port->pixel_parser + TEGRA_CSI_TPG_OFFSET;
		}
	}
	csi->plld = devm_clk_get(csi->dev, "pll_d");
	if (IS_ERR(csi->plld)) {
		dev_err(csi->dev, "Fail to get pll_d\n");
		return PTR_ERR(csi->plld);
	}
	csi->plld_dsi = devm_clk_get(csi->dev, "pll_d_dsi_out");
	if (IS_ERR(csi->plld_dsi)) {
		dev_err(csi->dev, "Fail to get pll_d_dsi_out\n");
		return PTR_ERR(csi->plld_dsi);
	}
	return 0;
}

int csi2_mipi_cal(struct tegra_csi_channel *chan)
{
	unsigned int lanes, num_ports, val, csi_port;
	struct tegra_csi_port *port;
	struct tegra_csi_device *csi = chan->csi;

	lanes = 0;
	num_ports = 0;

	nvhost_module_enable_clk(csi->dev);
	while (num_ports < chan->numports) {
		port = &chan->ports[num_ports];
		csi_port = port->num;
		dev_dbg(csi->dev, "Calibrate csi port %d\n", port->num);

		if (chan->numlanes == 2) {
			lanes |= CSIA << csi_port;
			csi_write(chan,
				TEGRA_CSI_CIL_OFFSET +
				TEGRA_CSI_CIL_PAD_CONFIG0, 0x0, csi_port >> 1);
			val = ((csi_port & 0x1) == PORT_A) ?
				CSI_A_PHY_CIL_ENABLE | CSI_B_PHY_CIL_NOP
				: CSI_B_PHY_CIL_ENABLE | CSI_A_PHY_CIL_NOP;
			csi_write(chan, TEGRA_CSI_PHY_CIL_COMMAND, val,
				csi_port >> 1);
		} else {
			lanes |= (CSIA | CSIB) << port->num;
			csi_write(chan, TEGRA_CSI_PHY_CIL_COMMAND,
				CSI_A_PHY_CIL_ENABLE | CSI_B_PHY_CIL_ENABLE,
				csi_port >> 1);
		}
		num_ports++;
	}
	if (!lanes) {
		dev_err(csi->dev,
			"Selected no CSI lane, cannot do calibration");
		return -EINVAL;
	}
	nvhost_module_disable_clk(csi->dev);
	return tegra_mipi_calibration(lanes);
}

int csi2_power_on(struct tegra_csi_device *csi)
{
	return 0;
}
int csi2_power_off(struct tegra_csi_device *csi)
{
	return 0;
}
const struct tegra_csi_fops csi2_fops = {
	.csi_power_on = csi2_power_on,
	.csi_power_off = csi2_power_off,
	.csi_start_streaming = csi2_start_streaming,
	.csi_stop_streaming = csi2_stop_streaming,
	.mipical = csi2_mipi_cal,
	.hw_init = csi2_hw_init,
};
EXPORT_SYMBOL(csi2_fops);
