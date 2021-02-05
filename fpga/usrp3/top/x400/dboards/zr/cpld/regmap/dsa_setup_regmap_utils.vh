//
// Copyright 2021 Ettus Research, A National Instruments Company
//
// SPDX-License-Identifier: LGPL-3.0-or-later
//
// Module: dsa_setup_regmap_utils.vh
// Description:
// The constants in this file are autogenerated by XmlParse.

//===============================================================================
// A numerically ordered list of registers and their HDL source files
//===============================================================================

  // TX0_DSA_ATR          : 0x0 (dsa_control.v)
  // TX1_DSA_ATR          : 0x400 (dsa_control.v)
  // RX0_DSA_ATR          : 0x800 (dsa_control.v)
  // RX1_DSA_ATR          : 0xC00 (dsa_control.v)
  // TX0_DSA_TABLE_SELECT : 0x1000 (dsa_control.v)
  // TX1_DSA_TABLE_SELECT : 0x1400 (dsa_control.v)
  // RX0_DSA_TABLE_SELECT : 0x1800 (dsa_control.v)
  // RX1_DSA_TABLE_SELECT : 0x1C00 (dsa_control.v)
  // TX0_DSA_TABLE        : 0x2000 (dsa_control.v)
  // TX1_DSA_TABLE        : 0x2400 (dsa_control.v)
  // RX0_DSA_TABLE        : 0x2800 (dsa_control.v)
  // RX1_DSA_TABLE        : 0x2C00 (dsa_control.v)

//===============================================================================
// RegTypes
//===============================================================================

  // DSA_TABLE_CONTROL Type (from dsa_control.v)
  localparam DSA_TABLE_CONTROL_SIZE = 32;
  localparam DSA_TABLE_CONTROL_MASK = 32'hFF;
  localparam TABLE_INDEX_SIZE = 8;  //DSA_TABLE_CONTROL:TABLE_INDEX
  localparam TABLE_INDEX_MSB  = 7;  //DSA_TABLE_CONTROL:TABLE_INDEX
  localparam TABLE_INDEX      = 0;  //DSA_TABLE_CONTROL:TABLE_INDEX

  // RX_DSA_CONTROL Type (from dsa_control.v)
  localparam RX_DSA_CONTROL_SIZE = 32;
  localparam RX_DSA_CONTROL_MASK = 32'hFFFF;
  localparam RX_DSA1_SIZE = 4;  //RX_DSA_CONTROL:RX_DSA1
  localparam RX_DSA1_MSB  = 3;  //RX_DSA_CONTROL:RX_DSA1
  localparam RX_DSA1      = 0;  //RX_DSA_CONTROL:RX_DSA1
  localparam RX_DSA2_SIZE = 4;  //RX_DSA_CONTROL:RX_DSA2
  localparam RX_DSA2_MSB  = 7;  //RX_DSA_CONTROL:RX_DSA2
  localparam RX_DSA2      = 4;  //RX_DSA_CONTROL:RX_DSA2
  localparam RX_DSA3_A_SIZE =  4;  //RX_DSA_CONTROL:RX_DSA3_A
  localparam RX_DSA3_A_MSB  = 11;  //RX_DSA_CONTROL:RX_DSA3_A
  localparam RX_DSA3_A      =  8;  //RX_DSA_CONTROL:RX_DSA3_A
  localparam RX_DSA3_B_SIZE =  4;  //RX_DSA_CONTROL:RX_DSA3_B
  localparam RX_DSA3_B_MSB  = 15;  //RX_DSA_CONTROL:RX_DSA3_B
  localparam RX_DSA3_B      = 12;  //RX_DSA_CONTROL:RX_DSA3_B

  // TX_DSA_CONTROL Type (from dsa_control.v)
  localparam TX_DSA_CONTROL_SIZE = 32;
  localparam TX_DSA_CONTROL_MASK = 32'h1F1F;
  localparam TX_DSA1_SIZE = 5;  //TX_DSA_CONTROL:TX_DSA1
  localparam TX_DSA1_MSB  = 4;  //TX_DSA_CONTROL:TX_DSA1
  localparam TX_DSA1      = 0;  //TX_DSA_CONTROL:TX_DSA1
  localparam TX_DSA2_SIZE =  5;  //TX_DSA_CONTROL:TX_DSA2
  localparam TX_DSA2_MSB  = 12;  //TX_DSA_CONTROL:TX_DSA2
  localparam TX_DSA2      =  8;  //TX_DSA_CONTROL:TX_DSA2

//===============================================================================
// Register Group DSA_SETUP_REGISTERS
//===============================================================================

  // TX0_DSA_ATR Register (from dsa_control.v)
  localparam TX0_DSA_ATR_COUNT = 256; // Number of elements in array

  // TX1_DSA_ATR Register (from dsa_control.v)
  localparam TX1_DSA_ATR_COUNT = 256; // Number of elements in array

  // RX0_DSA_ATR Register (from dsa_control.v)
  localparam RX0_DSA_ATR_COUNT = 256; // Number of elements in array

  // RX1_DSA_ATR Register (from dsa_control.v)
  localparam RX1_DSA_ATR_COUNT = 256; // Number of elements in array

  // TX0_DSA_TABLE_SELECT Register (from dsa_control.v)
  localparam TX0_DSA_TABLE_SELECT_COUNT = 256; // Number of elements in array

  // TX1_DSA_TABLE_SELECT Register (from dsa_control.v)
  localparam TX1_DSA_TABLE_SELECT_COUNT = 256; // Number of elements in array

  // RX0_DSA_TABLE_SELECT Register (from dsa_control.v)
  localparam RX0_DSA_TABLE_SELECT_COUNT = 256; // Number of elements in array

  // RX1_DSA_TABLE_SELECT Register (from dsa_control.v)
  localparam RX1_DSA_TABLE_SELECT_COUNT = 256; // Number of elements in array

  // TX0_DSA_TABLE Register (from dsa_control.v)
  localparam TX0_DSA_TABLE_COUNT = 256; // Number of elements in array

  // TX1_DSA_TABLE Register (from dsa_control.v)
  localparam TX1_DSA_TABLE_COUNT = 256; // Number of elements in array

  // RX0_DSA_TABLE Register (from dsa_control.v)
  localparam RX0_DSA_TABLE_COUNT = 256; // Number of elements in array

  // RX1_DSA_TABLE Register (from dsa_control.v)
  localparam RX1_DSA_TABLE_COUNT = 256; // Number of elements in array

  // Return the offset of an element of register array TX0_DSA_ATR
  function integer TX0_DSA_ATR (input integer i);
    TX0_DSA_ATR = (i * 'h4) + 'h0;
  endfunction

  // Return the offset of an element of register array TX1_DSA_ATR
  function integer TX1_DSA_ATR (input integer i);
    TX1_DSA_ATR = (i * 'h4) + 'h400;
  endfunction

  // Return the offset of an element of register array RX0_DSA_ATR
  function integer RX0_DSA_ATR (input integer i);
    RX0_DSA_ATR = (i * 'h4) + 'h800;
  endfunction

  // Return the offset of an element of register array RX1_DSA_ATR
  function integer RX1_DSA_ATR (input integer i);
    RX1_DSA_ATR = (i * 'h4) + 'hC00;
  endfunction

  // Return the offset of an element of register array TX0_DSA_TABLE_SELECT
  function integer TX0_DSA_TABLE_SELECT (input integer i);
    TX0_DSA_TABLE_SELECT = (i * 'h4) + 'h1000;
  endfunction

  // Return the offset of an element of register array TX1_DSA_TABLE_SELECT
  function integer TX1_DSA_TABLE_SELECT (input integer i);
    TX1_DSA_TABLE_SELECT = (i * 'h4) + 'h1400;
  endfunction

  // Return the offset of an element of register array RX0_DSA_TABLE_SELECT
  function integer RX0_DSA_TABLE_SELECT (input integer i);
    RX0_DSA_TABLE_SELECT = (i * 'h4) + 'h1800;
  endfunction

  // Return the offset of an element of register array RX1_DSA_TABLE_SELECT
  function integer RX1_DSA_TABLE_SELECT (input integer i);
    RX1_DSA_TABLE_SELECT = (i * 'h4) + 'h1C00;
  endfunction

  // Return the offset of an element of register array TX0_DSA_TABLE
  function integer TX0_DSA_TABLE (input integer i);
    TX0_DSA_TABLE = (i * 'h4) + 'h2000;
  endfunction

  // Return the offset of an element of register array TX1_DSA_TABLE
  function integer TX1_DSA_TABLE (input integer i);
    TX1_DSA_TABLE = (i * 'h4) + 'h2400;
  endfunction

  // Return the offset of an element of register array RX0_DSA_TABLE
  function integer RX0_DSA_TABLE (input integer i);
    RX0_DSA_TABLE = (i * 'h4) + 'h2800;
  endfunction

  // Return the offset of an element of register array RX1_DSA_TABLE
  function integer RX1_DSA_TABLE (input integer i);
    RX1_DSA_TABLE = (i * 'h4) + 'h2C00;
  endfunction
