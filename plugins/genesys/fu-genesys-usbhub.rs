// Copyright 2023 Adam.Chen <Adam.Chen@genesyslogic.com.tw>
// SPDX-License-Identifier: LGPL-2.1-or-later

#[derive(ValidateStream, Default)]
#[repr(C, packed)]
struct FuStructGenesysFirmwareHdr {
    reserved: [u8; 252],
    magic: [char; 4] == "XROM",
}

#[derive(ValidateStream, Default)]
#[repr(C, packed)]
struct FuStructGenesysDevFirmwareHdr {
    reserved: [u8; 252],
    magic: [char; 4] == "HOST",
}

#[derive(ValidateStream, Default)]
#[repr(C, packed)]
struct FuStructGenesysPdFirmwareHdr {
    reserved: [u8; 252],
    magic: [char; 4] == "PRDY",
}

// Tool String Descriptor
#[repr(u8)]
#[derive(ToString)]
enum FuGenesysTsVersion {
    Dynamic_9Byte = 0x30,
    Bonding,
    BondingQc,
    VendorSupport,
    MultiToken,
    Dynamic_2nd,
    Reserved,
    Dynamic_13Byte,
    BrandProject,
}

#[derive(Parse, ParseStream, New)]
#[repr(C, packed)]
struct FuStructGenesysTsStatic {
    tool_string_version: FuGenesysTsVersion,

    mask_project_code: [char; 4],
    mask_project_hardware: char,      // 0=a, 1=b...
    mask_project_firmware: [char; 2], // 01,02,03...
    mask_project_ic_type: [char; 6],  // 352310=GL3523-10 (ASCII string)

    running_project_code: [char; 4],
    running_project_hardware: char,
    running_project_firmware: [char; 2],
    running_project_ic_type: [char; 6],

    firmware_version: [char; 4], // MMmm=MM.mm (ASCII string)
}

#[derive(Parse)]
#[repr(C, packed)]
struct FuStructGenesysTsDynamicGl3523 {
    running_mode: char, // 'M' for mask code, the others for bank code

    ss_port_number: char, // super-speed port number
    hs_port_number: char, // high-speed port number

    ss_connection_status: char, // bit field. ON = DFP is a super-speed device
    hs_connection_status: char, // bit field. ON = DFP is a high-speed device
    fs_connection_status: char, // bit field. ON = DFP is a full-speed device
    ls_connection_status: char, // bit field. ON = DFP is a low-speed device

    charging: char,                  // bit field. ON = DFP is a charging port
    non_removable_port_status: char, // bit field. ON = DFP is a non-removable port

    //   Bonding reports Hardware register status for GL3523: (ASCII)
    //     2 / 4 ports         : 1 means 4 ports, 0 means 2 ports
    //     MTT / STT           : 1 means Multi Token Transfer, 0 means Single TT
    //     Type - C            : 1 means disable, 0 means enable
    //     QC                  : 1 means disable, 0 means enable
    //     Flash dump location : 1 means 32KB offset bank 1, 0 means 0 offset bank 0.

    //   Tool string Version 1:
    //     Bit3 : Flash dump location
    //     BIT2 : Type - C
    //     BIT1 : MTT / STT
    //     BIT0 : 2 / 4 ports

    //   Tool string Version 2 or newer :
    //     Bit4 : Flash dump location
    //     BIT3 : Type - C
    //     BIT2 : MTT / STT
    //     BIT1 : 2 / 4 ports
    //     BIT0 : QC
    //   Default use '0'~'F', plus Bit4 may over value, should extract that.
    bonding: char,
}

#[derive(Parse)]
#[repr(C, packed)]
struct FuStructGenesysTsDynamicGl3590 {
    running_mode: char,

    ss_port_number: char,
    hs_port_number: char,

    ss_connection_status: char,
    hs_connection_status: char,
    fs_connection_status: char,
    ls_connection_status: char,

    charging: char,
    non_removable_port_status: char,

    //   Bonding for GL3590-10/20:
    //     Bit7 : Flash dump location, 0 means bank 0, 1 means bank 1.
    bonding: u8,
}

#[derive(ToString)]
#[repr(u8)]
enum FuGenesysFwStatus {
    Mask = 0x30,
    Bank1,
    Bank2,
}

#[derive(Parse)]
#[repr(C, packed)]
struct FuStructGenesysTsDynamicGl359030 {
    running_mode: char,

    ss_port_number: char,
    hs_port_number: char,

    ss_connection_status: char,
    hs_connection_status: char,
    fs_connection_status: char,
    ls_connection_status: char,

    charging: char,
    non_removable_port_status: char,

    bonding: u8,

    hub_fw_status: FuGenesysFwStatus,
    dev_fw_status: FuGenesysFwStatus,
    dev_fw_version: u16le,
}

#[derive(Parse)]
#[repr(C, packed)]
struct FuStructGenesysTsDynamicGl3525 {
    running_mode: char,

    ss_port_number: char,
    hs_port_number: char,

    ss_connection_status: char,
    hs_connection_status: char,
    fs_connection_status: char,
    ls_connection_status: char,

    charging: char,
    non_removable_port_status: char,

    bonding: u8,

    hub_fw_status: FuGenesysFwStatus,
    pd_fw_status: FuGenesysFwStatus,
    pd_fw_version: u16le,
    dev_fw_status: FuGenesysFwStatus,
    dev_fw_version: u16le,
}

#[derive(Parse)]
#[repr(C, packed)]
struct FuStructGenesysTsFirmwareInfo {
    tool_version: [u8; 6],      // ISP tool defined by itself
    address_mode: u8,           // 3 or 4: support 3 or 4-bytes address, others are no meaning.
    build_fw_time: [char; 12],  // YYYYMMDDhhmm
    update_fw_time: [char; 12], // YYYYMMDDhhmm
}

#[derive(ToString)]
#[repr(u8)]
enum FuGenesysVsCodesignCheck {
    Unsupported = 0x30,
    Scaler,
    Fw,
    Master, // there is a master hub has Scaler or Hw, and this hub verified by the master.
    Reserved,
    Hw,
}

#[repr(u8)]
enum FuGenesysVsHidIsp {
    Unsupported = 0x30,
    Support,
    CodesignNReset, // Support Codesign ISP Bank2 FW without reset.
}

#[derive(New, Parse)]
#[repr(C, packed)]
struct FuStructGenesysTsVendorSupport {
    version: [char; 2],
    reserved1: [char; 8],
    codesign_check: FuGenesysVsCodesignCheck, // offset: 0x0a
    reserved2: [char; 4],
    hid_isp: FuGenesysVsHidIsp, // offset: 0x0f
    reserved3: [char; 15],
}

#[derive(Parse)]
#[repr(C, packed)]
struct FuStructGenesysTsBrandProject {
    project: [char; 15],
}

// Firmware info
#[derive(ToString)]
enum FuGenesysFwCodesign {
    None,
    Rsa,
    Ecdsa,
}

#[derive(ParseStream, ValidateStream, Default)]
#[repr(C, packed)]
struct FuStructGenesysFwCodesignInfoRsa {
    tag_n: u32be == 0x4E203D20, // 'N = '
    text_n: [char; 512],
    end_n: u16be == 0x0D0A,
    tag_e: u32be == 0x45203D20, // 'E = '
    text_e: [char; 6],
    end_e: u16be == 0x0D0A,
    signature: [u8; 256],
}

#[derive(Parse, Validate, Default)]
#[repr(C, packed)]
struct FuStructGenesysFwRsaPublicKeyText {
    tag_n: u32be == 0x4E203D20, // 'N = '
    text_n: [char; 512],
    end_n: u16be == 0x0D0A,
    tag_e: u32be == 0x45203D20, // 'E = '
    text_e: [char; 6],
    end_e: u16be == 0x0D0A,
}

#[derive(Parse, ParseStream, Validate, ValidateStream)]
#[repr(C, packed)]
struct FuStructGenesysFwCodesignInfoEcdsa {
    hash: [u8; 32],
    key: [u8; 64],
    signature: [u8; 64],
}

#[derive(Parse, Validate)]
#[repr(C, packed)]
struct FuStructGenesysFwEcdsaPublicKey {
    key: [u8; 64],
}

#[derive(ToString)]
enum FuGenesysFwType {
    Hub, // inside hub start
    DevBridge,
    Pd,
    Codesign, // inside hub end
    InsideHubCount,

    Scaler, // vendor support start

    Unknown = 0xff,
}
