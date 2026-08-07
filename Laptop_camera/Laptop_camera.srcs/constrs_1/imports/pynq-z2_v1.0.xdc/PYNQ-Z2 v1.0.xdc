## PYNQ-Z2 -- HDMI RX capture to DDR over VDMA
## Port names must match the top-level wrapper generated from your block design.
## Verify them with:  Open Elaborated Design -> I/O Ports

## ---------------------------------------------------------------------------
## HDMI RX -- TMDS differential pairs
## ---------------------------------------------------------------------------
set_property -dict { PACKAGE_PIN N18  IOSTANDARD TMDS_33 } [get_ports { TMDS_0_clk_p }]
set_property -dict { PACKAGE_PIN P19  IOSTANDARD TMDS_33 } [get_ports { TMDS_0_clk_n }]

set_property -dict { PACKAGE_PIN V20  IOSTANDARD TMDS_33 } [get_ports { TMDS_0_data_p[0] }]
set_property -dict { PACKAGE_PIN W20  IOSTANDARD TMDS_33 } [get_ports { TMDS_0_data_n[0] }]

set_property -dict { PACKAGE_PIN T20  IOSTANDARD TMDS_33 } [get_ports { TMDS_0_data_p[1] }]
set_property -dict { PACKAGE_PIN U20  IOSTANDARD TMDS_33 } [get_ports { TMDS_0_data_n[1] }]

set_property -dict { PACKAGE_PIN N20  IOSTANDARD TMDS_33 } [get_ports { TMDS_0_data_p[2] }]
set_property -dict { PACKAGE_PIN P20  IOSTANDARD TMDS_33 } [get_ports { TMDS_0_data_n[2] }]

## ---------------------------------------------------------------------------
## HDMI RX -- DDC (I2C, carries the EDID your laptop reads)
## ---------------------------------------------------------------------------
set_property -dict { PACKAGE_PIN U14  IOSTANDARD LVCMOS33 } [get_ports { hdmi_in_ddc_scl_io }]
set_property -dict { PACKAGE_PIN U15  IOSTANDARD LVCMOS33 } [get_ports { hdmi_in_ddc_sda_io }]

## ---------------------------------------------------------------------------
## HDMI RX -- Hot Plug Detect (driven high by xlconstant)
## ---------------------------------------------------------------------------
set_property -dict { PACKAGE_PIN T19  IOSTANDARD LVCMOS33 } [get_ports { hdmi_in_hpd[0] }]

## ---------------------------------------------------------------------------
## Input clock -- 720p60 pixel clock is 74.25 MHz -> 13.468 ns
## Change this if you target a different resolution:
##   640x480@60  = 25.175 MHz -> 39.722 ns
##   1280x720@60 = 74.250 MHz -> 13.468 ns
##   1920x1080@60 = 148.5 MHz ->  6.734 ns
## ---------------------------------------------------------------------------
create_clock -period 13.468 -name tmds_clk [get_ports TMDS_0_clk_p]

## The recovered pixel clock and the 100 MHz AXI clock are unrelated.
## The Video In to AXI4-Stream block handles the crossing, so tell the tools
## not to try to time paths between the two domains.
set_clock_groups -asynchronous \
  -group [get_clocks -include_generated_clocks tmds_clk] \
  -group [get_clocks -include_generated_clocks clk_fpga_0]
