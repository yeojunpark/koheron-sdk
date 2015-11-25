proc add_address_module {module_name bram_width clk} {

  set bd [current_bd_instance .]
  current_bd_instance [create_bd_cell -type hier $module_name]

  create_bd_pin -dir I                  clk
  create_bd_pin -dir I -from 32   -to 0 cfg
  create_bd_pin -dir O -from 15   -to 0 addr
  create_bd_pin -dir O -from 15   -to 0 addr_delayed
  create_bd_pin -dir O                  start

  # Add address counter
  cell xilinx.com:ip:c_counter_binary:12.0 base_counter \
    [list Output_Width [expr $bram_width+2] Increment_Value 4 SCLR true] \
    [list CLK clk Q addr]

  cell pavel-demin:user:edge_detector:1.0 reset_base_counter {} \
    [list clk clk dout base_counter/SCLR]

  cell pavel-demin:user:edge_detector:1.0 edge_detector {} [list clk clk dout start]

  cell xilinx.com:ip:c_shift_ram:12.0 delay_addr \
    [list ShiftRegType Variable_Length_Lossless Width [expr $bram_width+2]] \
    [list D base_counter/Q CLK clk Q addr_delayed]

  # Configuration registers

  cell xilinx.com:ip:xlslice:1.0 reset_base_counter_slice \
    [list DIN_WIDTH 32 DIN_FROM 0 DIN_TO 0]               \
    [list Din cfg Dout reset_base_counter/din]

  cell xilinx.com:ip:xlslice:1.0 start_slice \
    [list DIN_WIDTH 32 DIN_FROM 1 DIN_TO 1]  \
    [list Din cfg Dout edge_detector/Din]

  cell xilinx.com:ip:xlslice:1.0 addr_delay_slice \
    [list DIN_WIDTH 32 DIN_FROM 5 DIN_TO 2]       \
    [list Din cfg Dout delay_addr/A]

  #

  current_bd_instance $bd

}