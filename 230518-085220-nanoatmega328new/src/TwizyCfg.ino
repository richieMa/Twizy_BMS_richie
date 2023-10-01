/**
 * ==========================================================================
 * Twizy/SEVCON configuration shell
 * ==========================================================================
 * 
 * Based on the OVMS Twizy firmware:
 * https://github.com/openvehicles/Open-Vehicle-Monitoring-System
 * 
 * Author: Michael Balzer <dexter@dexters-web.de>
 * 
 * Libraries used:
 *  - MCP_CAN: https://github.com/coryjfowler/MCP_CAN_lib
 *  - iso-tp: https://github.com/altelch/iso-tp
 * 
 * License:
 *  This is free software under GNU Lesser General Public License (LGPL)
 *  https://www.gnu.org/licenses/lgpl.html
 *  
 */
#define TWIZY_CFG_VERSION "V2.1.1 (2023-03-05)"

#include <EEPROM.h>
#include <mcp_can.h>
#include <mcp_can_dfs.h>
#include <iso-tp.h>
#include <utils.h>
#include <CANopen.h>
#include <Tuning.h>
#include <TwizyCfg_config.h>
#include <SPI.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>


// CAN interface:
MCP_CAN CAN(TWIZY_CAN_CS_PIN);

// ISO-TP:
IsoTp isotp(&CAN, TWIZY_CAN_IRQ_PIN);
struct Message_t tpMsg;

// Output buffers:
char net_scratchpad[200];
char net_msg_scratchpad[200];


// --------------------------------------------------------------------
// COMMAND DISPATCHER:
//

enum cfg_command_id {
  cmdNone = 0,
  cmdHelp,
  cmdRead, cmdReadString, cmdWrite, cmdWriteOnly,
  cmdPreOp, cmdOp,
  cmdSet, cmdReset, cmdGet, cmdInfo, cmdSave, cmdLoad,
  cmdDrive, cmdRecup, cmdRamps, cmdRampLimits, cmdSmooth,
  cmdSpeed, cmdPower, cmdTSMap, cmdBrakelight,
  cmdDiagAddress, cmdDiagRequest
};

enum cfg_command_mode {
  modeOffline, modeLogin, modePreOp
};

struct cfg_command {
  char                  cmd[11];
  cfg_command_id        id;
  cfg_command_mode      mode;
};

const cfg_command command_table[] PROGMEM = {
  
  { "?", cmdHelp, modeOffline },
  { "HELP", cmdHelp, modeOffline },
  
  { "READ", cmdRead, modeLogin },
  { "R", cmdRead, modeLogin },
  { "READS", cmdReadString, modeLogin },
  { "RS", cmdReadString, modeLogin },
  { "WRITE", cmdWrite, modeLogin },
  { "W", cmdWrite, modeLogin },
  { "WRITEO", cmdWriteOnly, modeLogin },
  { "WO", cmdWriteOnly, modeLogin },
  { "PRE", cmdPreOp, modeLogin },
  { "P", cmdPreOp, modeLogin },
  { "OP", cmdOp, modeLogin },
  { "O", cmdOp, modeLogin },
  
  { "SET", cmdSet, modeOffline },
  { "RESET", cmdReset, modeOffline },
  { "GET", cmdGet, modeOffline },
  { "INFO", cmdInfo, modeOffline },
  { "SAVE", cmdSave, modeOffline },
  { "LOAD", cmdLoad, modeLogin },
  
  { "DRIVE", cmdDrive, modeLogin },
  { "RECUP", cmdRecup, modeLogin },
  { "RAMPS", cmdRamps, modeLogin },
  { "RAMPL", cmdRampLimits, modeLogin },
  { "SMOOTH", cmdSmooth, modeLogin },
  
  { "SPEED", cmdSpeed, modePreOp },
  { "POWER", cmdPower, modePreOp },
  { "TSMAP", cmdTSMap, modePreOp },
  { "BRAKELIGHT", cmdBrakelight, modePreOp },
  
  { "DA", cmdDiagAddress, modeOffline },
  { "DR", cmdDiagRequest, modeOffline },
  
};

#define COMMAND_COUNT (sizeof(command_table)/sizeof(cfg_command))


bool exec(char *cmdline)
{
  UINT err;
  char *s;
  char *t;
  bool go_op_onexit = true;
  char *arguments;
  
  int arg[5] = {-1,-1,-1,-1,-1};
  INT8 arg2[4] = {-1,-1,-1,-1};
  long data;
  char maps[4] = {'D','N','B',0};
  UINT8 i;

  arguments = net_sms_initargs(cmdline);

  // convert cmd to upper-case:
  for (s=cmdline; ((*s!=0)&&(*s!=' ')); s++)
    if ((*s > 0x60) && (*s < 0x7b)) *s=*s-0x20;

  
  //
  // Identify command:
  //
  
  cfg_command cmd;
  
  for (i = 0; i < COMMAND_COUNT; i++ ) {
    memcpy_P(&cmd, &command_table[i], sizeof(cfg_command));
    if (strcmp(cmdline, cmd.cmd) == 0) {
      break;
    }
  }
  
  if (i == COMMAND_COUNT) {
    cmd.id = cmdNone;
    cmd.mode = modeOffline;
  }

  if (cmd.id == cmdHelp) {
    Serial.print(F("\n"
      "Twizy-Cfg " TWIZY_CFG_VERSION "\n"
      "\n"
      "Commands:\n"
      " ?, help                  -- output this info\n"
      " r <id> <sub>             -- read SDO register (numerical)\n"
      " rs <id> <sub>            -- read SDO register (string)\n"
      " w <id> <sub> <val>       -- write SDO register (numerical) & show old value\n"
      " wo <id> <sub> <val>      -- write-only SDO register (numerical)\n"
      " p                        -- preop mode\n"
      " o                        -- op mode\n"
      "(Hint: standard OVMS syntax also accepted)\n"
      "\n"
      " set <prf> <b64>          -- set profile from base64\n"
      " reset <prf>              -- reset profile\n"
      " get <prf>                -- get profile base64\n"
      " info                     -- show main profile values\n"
      " save <prf>               -- save config to profile\n"
      " load <prf>               -- load config from profile\n"
      "\n"
      " drive <prc>              -- set drive level\n"
      " recup <ntr> <brk>        -- set recuperation levels neutral & brake\n"
      " ramps <st> <ac> <dc> <nt> <br> -- set ramp levels\n"
      " rampl <ac> <dc>          -- set ramp limits\n"
      " smooth <prc>             -- set smoothing\n"
      "\n"
      " speed <max> <warn>       -- set max & warn speed\n"
      " power <trq> <pw1> <pw2> <cur> -- set torque, power & current levels\n"
      " tsmap <DNB> <pt1..4>     -- set torque speed maps\n"
      " brakelight <on> <off>    -- set brakelight accel levels\n"
      "\n"
      " da <sendid> <recvid>     -- set OBD2 device address\n"
      " dr <hexstring>           -- send OBD2 request\n"
      "\n"
      "See OVMS manual & command overview for details.\n"
      "Note: <id> and <sub> are hexadecimal, <val> are decimal\n"
      "Examples:\n"
      " rs 1008 0                -- read SEVCON firmware name\n"
      " w 2920 3 325             -- set neutral recup level to 32.5%\n"
      "\n"
      ));
    return false;
  }


  //
  // Prepare command:
  //

  err = 0;
  
  // common reply intro:
  s = stp_ram(net_scratchpad, cmdline);
  s = stp_rom(s, ": ");
  
  if (cmd.mode >= modeLogin) {
    // login:
    if (err = login(1)) {
      s = vehicle_twizy_fmt_err(s, err);
    }
  }
  
  if (!err && cmd.mode >= modePreOp) {
    // enter config mode:
    if (err = configmode(1)) {
      s = vehicle_twizy_fmt_err(s, err);
    }
  }
  
  //
  // Execute command:
  //

  if (!err) switch (cmd.id) {


    case cmdPreOp:
      // enter config mode
      if (err = configmode(1))
        s = vehicle_twizy_fmt_err(s, err);
      else
        s = stp_rom(s, "OK");
      go_op_onexit = false;
      break;


    case cmdOp:
      // leave config mode
      if (err = configmode(0))
        s = vehicle_twizy_fmt_err(s, err);
      else
        s = stp_rom(s, "OK");
      go_op_onexit = false;
      break;


    case cmdRead:
    case cmdReadString:
      // R index_hex subindex_hex
      // RS index_hex subindex_hex
      if (arguments = net_sms_nextarg(arguments))
        arg[0] = (int)axtoul(arguments);
      if (arguments = net_sms_nextarg(arguments))
        arg[1] = (int)axtoul(arguments);

      if (!arguments) {
        s = stp_rom(s, "ERROR: Too few args");
      }
      else {
        if (cmd.id == cmdRead) {
          // READ:
          if (err = readsdo(arg[0], arg[1])) {
            s = vehicle_twizy_fmt_err(s, err);
          }
          else {
            s = vehicle_twizy_fmt_sdo(s);
            s = stp_ul(s, " = ", twizy_sdo.data);
          }
        }
        else {
          // READS: SMS intro 'CFG READS: 0x1234.56=' = 21 chars, 139 remaining
          if (err = readsdo_buf(arg[0], arg[1], (byte*)net_msg_scratchpad, (i=139, &i))) {
            s = vehicle_twizy_fmt_err(s, err);
          }
          else {
            net_msg_scratchpad[139-i] = 0;
            s = stp_x(s, "0x", arg[0]);
            s = stp_sx(s, ".", arg[1]);
            s = stp_s(s, "=", net_msg_scratchpad);
          }
        }
      }

      go_op_onexit = false;
      break;


    case cmdWrite:
    case cmdWriteOnly:
      // W index_hex subindex_hex data_dec
      // WO index_hex subindex_hex data_dec
      if (arguments = net_sms_nextarg(arguments))
        arg[0] = (int)axtoul(arguments);
      if (arguments = net_sms_nextarg(arguments))
        arg[1] = (int)axtoul(arguments);
      if (arguments = net_sms_nextarg(arguments))
        data = atol(arguments);

      if (!arguments) {
        s = stp_rom(s, "ERROR: Too few args");
      }
      else {

        if (cmd.id == cmdWriteOnly) {
          // WRITEONLY:

          // write new value:
          if (err = writesdo(arg[0], arg[1], data)) {
            s = vehicle_twizy_fmt_err(s, err);
          }
          else {
            s = stp_rom(s, "OK: ");
            s = vehicle_twizy_fmt_sdo(s);
          }
        }

        else {
          // READ-WRITE:

          // read old value:
          if (err = readsdo(arg[0], arg[1])) {
            s = vehicle_twizy_fmt_err(s, err);
          }
          else {
            // read ok:
            s = stp_rom(s, "OLD:");
            s = vehicle_twizy_fmt_sdo(s);
            s = stp_ul(s, " = ", twizy_sdo.data);

            // write new value:
            if (err = writesdo(arg[0], arg[1], data))
              s = vehicle_twizy_fmt_err(s, err);
            else
              s = stp_ul(s, " => NEW: ", data);
          }
        }
      }

      go_op_onexit = false;
      break;


    case cmdDiagAddress:
      // DA tx_id_hex rx_id_hex
      if (arguments = net_sms_nextarg(arguments))
        arg[0] = (int)axtoul(arguments);
      if (arguments = net_sms_nextarg(arguments))
        arg[1] = (int)axtoul(arguments);

      if (!arguments) {
        s = stp_rom(s, "ERROR: Too few args");
      }
      else {
        tpMsg.tx_id = arg[0];
        tpMsg.rx_id = arg[1];
        CAN.init_Filt(1, 0, tpMsg.rx_id << 16);
        s = stp_rom(s, "OK");
      }
      go_op_onexit = false;
      break;


    case cmdDiagRequest:
      // DR hexstring
      if (arguments = net_sms_nextarg(arguments)) {
        if (strlen(arguments) & 1) {
          // input length is odd:
          s = stp_rom(s, "ERROR: invalid hex string length");
        }
        else {
          // parse hexstring into scratchpad:
          t = (char *) net_msg_scratchpad;
          maps[2] = 0;
          while (arguments[0]) {
            maps[0] = arguments[0];
            maps[1] = arguments[1];
            *t++ = axtoul(maps);
            arguments += 2;
          }
        }
      }

      if (!arguments) {
        s = stp_rom(s, "ERROR: Too few args");
      }
      else {
        // send request:
        tpMsg.Buffer = (uint8_t *)net_msg_scratchpad;
        tpMsg.len = (t - (char *)net_msg_scratchpad);
        if (err = isotp.send(&tpMsg)) {
          s = stp_i(s, "ERROR: isotp.send error code ", err);
        }
        else {
          // read response into scratchpad:
          tpMsg.Buffer = (uint8_t *)net_msg_scratchpad;
          tpMsg.len = 0;
          isotp.receive(&tpMsg);
          if (tpMsg.tp_state != ISOTP_FINISHED) {
            s = stp_i(s, "ERROR: isotp.receive error code ", tpMsg.tp_state);
          }
          else {
            // output response as hexstring:
            Serial.print(net_scratchpad);
            net_scratchpad[0] = 0;
            for (i=0; i<tpMsg.len; i++) {
              if ((byte)net_msg_scratchpad[i] < 0x10)
                Serial.print(F("0"));
              Serial.print((byte)net_msg_scratchpad[i], HEX);
            }
            break;
          }
        }
      }

      go_op_onexit = false;
      break;


    case cmdSet:
    case cmdReset:
      // SET [nr] [base64data]: set complete profile
      // RESET [nr]: reset profile (= SET without data)
      //   nr 1..3 = update EEPROM directly (no SEVCON access)
      //   else update working set & try to apply
      
      if (arguments = net_sms_nextarg(arguments))
        arg[0] = atoi(arguments);
      
      // we're a little bit out of RAM...
      // max cmd length is "CFG SET n " + 88 b64 chars + \0 = 99 chars.
      // use upper 90 bytes of net_scratchpad as the b64 codec buffer:
      t = net_scratchpad + 110;
      memset(t, 0, 90);
      *t = 1; // checksum for cleared/reset profile
      
      i = 0;
      if (arguments = net_sms_nextarg(arguments))
        i = base64decode(arguments, (byte*)t);
      
      if ((BYTE) t[0] != vehicle_twizy_cfg_calc_checksum((BYTE *)t)) {
        s = stp_rom(s, "ERROR: wrong checksum");
      }
      else if (arg[0] >= 1 && arg[0] <= 3) {
        // update EEPROM slot:
        //par_setbin(PARAM_PROFILE_S + ((arg[0]-1)<<1), t, 64);
        EEPROM.put(arg[0] * 64, *((cfg_profile*)t));
        // signal "unsaved" if WS now differs from stored profile:
        if (arg[0] == twizy_cfg.profile_user)
          twizy_cfg.unsaved = 1;
        s = stp_i(s, "OK #", arg[0]);
      }
      else {
        // update working set:
        memcpy((void *)&twizy_cfg_profile, (void *)t, sizeof(twizy_cfg_profile));
        // signal "unsaved" if profile modified or custom profile active:
        twizy_cfg.unsaved = (i > 0) || (twizy_cfg.profile_user > 0);
        // apply changed working set:
        err = vehicle_twizy_cfg_applyprofile(twizy_cfg.profile_user);
        s = vehicle_twizy_fmt_switchprofileresult(s, -1, err);
      }
      break;


    case cmdGet:
      // GET [nr]: get complete profile (base64 encoded)
      //  nr 1..3 = directly from EEPROM
      //  else working set
  
      if (arguments = net_sms_nextarg(arguments))
        arg[0] = atoi(arguments);
      
      if (arg[0] >= 1 && arg[0] <= 3) {
        // read from EEPROM:
        // we're a little bit out of RAM...
        // max response length is "CFG GET: #n= " + 88 b64 chars + \0 = 102 chars.
        // use upper 90 bytes of net_scratchpad as the b64 codec buffer:
        t = net_scratchpad + 110;
        //par_getbin(PARAM_PROFILE_S + ((arg[0]-1)<<1), t, sizeof(twizy_cfg_profile));
        EEPROM.get(arg[0] * 64, *((cfg_profile*)t));
        s = stp_i(s, "#", arg[0]);
      }
      else {
        // read from working set:
        twizy_cfg_profile.checksum = vehicle_twizy_cfg_calc_checksum((BYTE *)&twizy_cfg_profile);
        t = (char *) &twizy_cfg_profile;
        s = stp_rom(s, "WS");
      }
      
      s = stp_rom(s, "= ");
      s = base64encode((byte *)t, sizeof(twizy_cfg_profile), s);
      break;


    case cmdInfo:
      // INFO: output main params
  
      s = stp_i(s, "#", twizy_cfg.profile_user);
      if (twizy_cfg.unsaved)
        s = stp_rom(s, "/WS");
  
      s = stp_i(s, " SPEED ", cfgparam(speed));
      s = stp_i(s, " ", cfgparam(warn));
  
      s = stp_i(s, " POWER ", cfgparam(torque));
      s = stp_i(s, " ", cfgparam(power_low));
      s = stp_i(s, " ", cfgparam(power_high));
      s = stp_i(s, " ", cfgparam(current));
  
      s = stp_i(s, " DRIVE ", cfgparam(drive));
      s = stp_i(s, " ", cfgparam(autodrive_ref));
      s = stp_i(s, " ", cfgparam(autodrive_minprc));
      
      s = stp_i(s, " RECUP ", cfgparam(neutral));
      s = stp_i(s, " ", cfgparam(brake));
      s = stp_i(s, " ", cfgparam(autorecup_ref));
      s = stp_i(s, " ", cfgparam(autorecup_minprc));
  
      s = stp_i(s, " RAMPS ", cfgparam(ramp_start));
      s = stp_i(s, " ", cfgparam(ramp_accel));
      s = stp_i(s, " ", cfgparam(ramp_decel));
      s = stp_i(s, " ", cfgparam(ramp_neutral));
      s = stp_i(s, " ", cfgparam(ramp_brake));
  
      s = stp_i(s, " SMOOTH ", cfgparam(smooth));
      break;


    case cmdSave:
      // SAVE [nr]: save profile to EEPROM
  
      if (arguments = net_sms_nextarg(arguments))
        arg[0] = atoi(arguments);
      else
        arg[0] = twizy_cfg.profile_user; // save as current profile
  
      if (vehicle_twizy_cfg_writeprofile(arg[0]) == FALSE) {
        s = stp_i(s, "ERROR: wrong key #", arg[0]);
      }
      else {
        s = stp_i(s, "OK saved as #", arg[0]);
  
        // make destination new current:
        //par_set(PARAM_PROFILE, s-1);
        i = arg[0];
        EEPROM.put(PARAM_PROFILE, i);
        twizy_cfg.profile_user = arg[0];
        twizy_cfg.profile_cfgmode = arg[0];
        twizy_cfg.unsaved = 0;
      }
      break;


    case cmdLoad:
      // LOAD [nr]: load/restore profile from EEPROM

      if (arguments = net_sms_nextarg(arguments))
        arg[0] = atoi(arguments);
      else
        arg[0] = twizy_cfg.profile_user; // restore current profile

      err = vehicle_twizy_cfg_switchprofile(arg[0]);
      s = vehicle_twizy_fmt_switchprofileresult(s, arg[0], err);
      break;


    case cmdDrive:
      // DRIVE [max_prc] [autopower_ref] [autopower_minprc]

      if (arguments = net_sms_nextarg(arguments))
        arg[0] = atoi(arguments);

      // autopower drive 100% reference & min prc:
      if (arguments = net_sms_nextarg(arguments))
        arg[1] = atoi(arguments);
      if (arguments = net_sms_nextarg(arguments))
        arg[2] = atoi(arguments);

      if (err = vehicle_twizy_cfg_drive(arg[0], arg[1], arg[2])) {
        s = vehicle_twizy_fmt_err(s, err);
      }
      else {
        // update profile:
        twizy_cfg_profile.drive = cfgvalue(arg[0]);
        twizy_cfg_profile.autodrive_ref = cfgvalue(arg[1]);
        twizy_cfg_profile.autodrive_minprc = cfgvalue(arg[2]);
        twizy_cfg.unsaved = 1;

        // success message:
        s = stp_rom(s, "OK");
      }
      break;


    case cmdRecup:
      // RECUP [neutral_prc] [brake_prc] [autopower_ref] [autopower_minprc]

      if (arguments = net_sms_nextarg(arguments))
        arg[0] = atoi(arguments);

      if (arguments = net_sms_nextarg(arguments))
        arg[1] = atoi(arguments);
      else
        arg[1] = arg[0];

      if (arguments = net_sms_nextarg(arguments))
        arg[2] = atoi(arguments);
      if (arguments = net_sms_nextarg(arguments))
        arg[3] = atoi(arguments);

      if (err = vehicle_twizy_cfg_recup(arg[0], arg[1], arg[2], arg[3])) {
        s = vehicle_twizy_fmt_err(s, err);
      }
      else {
        // update profile:
        twizy_cfg_profile.neutral = cfgvalue(arg[0]);
        twizy_cfg_profile.brake = cfgvalue(arg[1]);
        twizy_cfg_profile.autorecup_ref = cfgvalue(arg[2]);
        twizy_cfg_profile.autorecup_minprc = cfgvalue(arg[3]);
        twizy_cfg.unsaved = 1;

        // success message:
        s = stp_rom(s, "OK");
      }
      break;


    case cmdRamps:
      // RAMPS [start_prc] [accel_prc] [decel_prc] [neutral_prc] [brake_prc]

      if (arguments = net_sms_nextarg(arguments))
        arg[0] = atoi(arguments);
      if (arguments = net_sms_nextarg(arguments))
        arg[1] = atoi(arguments);
      if (arguments = net_sms_nextarg(arguments))
        arg[2] = atoi(arguments);
      if (arguments = net_sms_nextarg(arguments))
        arg[3] = atoi(arguments);
      if (arguments = net_sms_nextarg(arguments))
        arg[4] = atoi(arguments);

      if (err = vehicle_twizy_cfg_ramps(arg[0], arg[1], arg[2], arg[3], arg[4])) {
        s = vehicle_twizy_fmt_err(s, err);
      }
      else {
        // update profile:
        twizy_cfg_profile.ramp_start = cfgvalue(arg[0]);
        twizy_cfg_profile.ramp_accel = cfgvalue(arg[1]);
        twizy_cfg_profile.ramp_decel = cfgvalue(arg[2]);
        twizy_cfg_profile.ramp_neutral = cfgvalue(arg[3]);
        twizy_cfg_profile.ramp_brake = cfgvalue(arg[4]);
        twizy_cfg.unsaved = 1;

        // success message:
        s = stp_rom(s, "OK");
      }
      break;


    case cmdRampLimits:
      // RAMPL [accel_prc] [decel_prc]

      if (arguments = net_sms_nextarg(arguments))
        arg[0] = atoi(arguments);
      if (arguments = net_sms_nextarg(arguments))
        arg[1] = atoi(arguments);

      if (err = vehicle_twizy_cfg_rampl(arg[0], arg[1])) {
        s = vehicle_twizy_fmt_err(s, err);
      }
      else {
        // update profile:
        twizy_cfg_profile.ramplimit_accel = cfgvalue(arg[0]);
        twizy_cfg_profile.ramplimit_decel = cfgvalue(arg[1]);
        twizy_cfg.unsaved = 1;

        // success message:
        s = stp_rom(s, "OK");
      }
      break;


    case cmdSmooth:
      // SMOOTH [prc]

      if (arguments = net_sms_nextarg(arguments))
        arg[0] = atoi(arguments);

      if (err = vehicle_twizy_cfg_smoothing(arg[0])) {
        s = vehicle_twizy_fmt_err(s, err);
      }
      else {
        // update profile:
        twizy_cfg_profile.smooth = cfgvalue(arg[0]);
        twizy_cfg.unsaved = 1;

        // success message:
        s = stp_rom(s, "OK");
      }
      break;


    case cmdSpeed:
      // SPEED [max_kph] [warn_kph]

      if (arguments = net_sms_nextarg(arguments))
        arg[0] = atoi(arguments);
      if (arguments = net_sms_nextarg(arguments))
        arg[1] = atoi(arguments);

      if ((err = vehicle_twizy_cfg_speed(arg[0], arg[1])) == 0)
        err = vehicle_twizy_cfg_makepowermap();

      if (err) {
        s = vehicle_twizy_fmt_err(s, err);
      }
      else {
        // update profile:
        twizy_cfg_profile.speed = cfgvalue(arg[0]);
        twizy_cfg_profile.warn = cfgvalue(arg[1]);
        twizy_cfg.unsaved = 1;

        // success message:
        s = stp_rom(s, "OK, power cycle to activate!");
      }
      break;


    case cmdPower:
      // POWER [trq_prc] [pwr_lo_prc] [pwr_hi_prc] [curr_prc]

      if (arguments = net_sms_nextarg(arguments))
        arg[0] = atoi(arguments);

      if (arguments = net_sms_nextarg(arguments))
        arg[1] = atoi(arguments);
      else
        arg[1] = arg[0];

      if (arguments = net_sms_nextarg(arguments))
        arg[2] = atoi(arguments);
      else
        arg[2] = arg[1];

      if (arguments = net_sms_nextarg(arguments))
        arg[3] = atoi(arguments);

      if ((err = vehicle_twizy_cfg_power(arg[0], arg[1], arg[2], arg[3])) == 0)
        err = vehicle_twizy_cfg_makepowermap();

      if (err) {
        s = vehicle_twizy_fmt_err(s, err);
      }
      else {
        // update profile:
        twizy_cfg_profile.torque = cfgvalue(arg[0]);
        twizy_cfg_profile.power_low = cfgvalue(arg[1]);
        twizy_cfg_profile.power_high = cfgvalue(arg[2]);
        twizy_cfg_profile.current = cfgvalue(arg[3]);
        twizy_cfg.unsaved = 1;

        // success message:
        s = stp_rom(s, "OK, power cycle to activate!");
      }
      break;


    case cmdTSMap:
      // TSMAP [maps] [t1_prc[@t1_spd]] [t2_prc[@t2_spd]] [t3_prc[@t3_spd]] [t4_prc[@t4_spd]]

      if (arguments = net_sms_nextarg(arguments)) {
        for (i=0; i<3 && arguments[i]; i++)
          maps[i] = arguments[i] & ~0x20;
        maps[i] = 0;
      }
      for (i=0; i<4; i++)
      {
        if (arguments = net_sms_nextarg(arguments))
        {
          arg[i] = atoi(arguments);
          if (cmdline = strchr(arguments, '@'))
            arg2[i] = atoi(cmdline+1);
        }
      }

      for (i=0; maps[i]; i++) {
        if (err = vehicle_twizy_cfg_tsmap(maps[i],
                    arg[0], arg[1], arg[2], arg[3],
                    arg2[0], arg2[1], arg2[2], arg2[3]))
          break;

        // update profile:
        err = (maps[i]=='D') ? 0 : ((maps[i]=='N') ? 1 : 2);
        twizy_cfg_profile.tsmap[err].prc1 = cfgvalue(arg[0]);
        twizy_cfg_profile.tsmap[err].prc2 = cfgvalue(arg[1]);
        twizy_cfg_profile.tsmap[err].prc3 = cfgvalue(arg[2]);
        twizy_cfg_profile.tsmap[err].prc4 = cfgvalue(arg[3]);
        twizy_cfg_profile.tsmap[err].spd1 = cfgvalue(arg2[0]);
        twizy_cfg_profile.tsmap[err].spd2 = cfgvalue(arg2[1]);
        twizy_cfg_profile.tsmap[err].spd3 = cfgvalue(arg2[2]);
        twizy_cfg_profile.tsmap[err].spd4 = cfgvalue(arg2[3]);
        err = 0;
      }

      if (err) {
        s = stp_rom(s, "MAP ");
        *s++ = maps[i]; *s = 0;
        s = vehicle_twizy_fmt_err(s, err);
      }
      else {
        twizy_cfg.unsaved = 1;
        s = stp_rom(s, "OK");
      }
      break;


    case cmdBrakelight:
      // BRAKELIGHT [on_lev] [off_lev]

      if (arguments = net_sms_nextarg(arguments))
        arg[0] = atoi(arguments);

      if (arguments = net_sms_nextarg(arguments))
        arg[1] = atoi(arguments);
      else
        arg[1] = arg[0];

      if (err = vehicle_twizy_cfg_brakelight(arg[0], arg[1])) {
        s = vehicle_twizy_fmt_err(s, err);
      }
      else {
        // update profile:
        twizy_cfg_profile.brakelight_on = cfgvalue(arg[0]);
        twizy_cfg_profile.brakelight_off = cfgvalue(arg[1]);
        twizy_cfg.unsaved = 1;

        // success message:
        s = stp_rom(s, "OK");
      }
      break;


    default:
      // unknown command
      s = stp_rom(s, "Unknown command");
      break;
  }


  // 
  // FINISH:
  //
  
  // go operational?
  if (!err && go_op_onexit)
    configmode(0);
    
  Serial.println(net_scratchpad);

  return true;
}


// --------------------------------------------------------------------
// MAIN
//

String inputString = "";         // a string to hold incoming data
boolean stringComplete = false;  // whether the string is complete


void setup() {
  
  Serial.begin(1000000);
  while (!Serial) {
    ; // wait for serial port to connect. Needed for native USB port only
  }
  
  // reserve 200 bytes for the inputString:
  inputString.reserve(200);

  //
  // Init Twizy CAN interface
  //
  
  while (CAN.begin(MCP_STDEXT, CAN_500KBPS, TWIZY_CAN_MCP_FREQ) != CAN_OK) {
    Serial.println(F("setup: waiting for CAN connection..."));
    delay(1000);
  }
  
  // Set filters:
  
  CAN.init_Mask(0, 0, 0x07FF0000);
  CAN.init_Filt(0, 0, 0x05810000); // CANopen response node 1
  CAN.init_Filt(1, 0, 0x00000000);
  
  CAN.init_Mask(1, 0, 0x07FF0000);
  CAN.init_Filt(2, 0, 0x00000000);
  CAN.init_Filt(3, 0, 0x00000000);
  CAN.init_Filt(4, 0, 0x00000000);
  CAN.init_Filt(5, 0, 0x00000000);
  
  CAN.setMode(MCP_NORMAL);

  // Init tuning module:

  vehicle_twizy_init();

  // Output info & prompt:
  exec((char *) "?");
  Serial.print("\n> ");
}


void loop() {
  
  if (stringComplete) {
    
    // execute command:
    Serial.println(inputString);
    exec((char *) inputString.c_str());
    Serial.print("\n> ");
    
    // clear the string:
    inputString = "";
    stringComplete = false;
  }
  
}


/*
 SerialEvent occurs whenever a new data comes in the
 hardware serial RX.  This routine is run between each
 time loop() runs, so using delay inside loop can delay
 response.  Multiple bytes of data may be available.
 */
void serialEvent() {
  
  while (Serial.available()) {
    char inChar = (char)Serial.read();
    if (inChar == '\r' || inChar == '\n') {
      stringComplete = (inputString.length() > 0);
    }
    else if (inChar >= 32) {
      inputString += inChar;
    }
  }
  
}
