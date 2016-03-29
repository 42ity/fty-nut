/*  =========================================================================
    ups_status - ups status converting functions

    Copyright (C) 2014 - 2015 Eaton                                        
                                                                           
    This program is free software; you can redistribute it and/or modify   
    it under the terms of the GNU General Public License as published by   
    the Free Software Foundation; either version 2 of the License, or      
    (at your option) any later version.                                    
                                                                           
    This program is distributed in the hope that it will be useful,        
    but WITHOUT ANY WARRANTY; without even the implied warranty of         
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the          
    GNU General Public License for more details.                           
                                                                           
    You should have received a copy of the GNU General Public License along
    with this program; if not, write to the Free Software Foundation, Inc.,
    51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.            
    =========================================================================
*/

#ifndef UPS_STATUS_H_INCLUDED
#define UPS_STATUS_H_INCLUDED

// Original author: Tomas Halman
// functions to work with ups status string representation as it is used in
// networkupstools

// following definition is taken as it is from network ups tool project (dummy-ups.h)
#define STATUS_CAL             (1 << 0)        /* calibration */
#define STATUS_TRIM            (1 << 1)        /* SmartTrim */
#define STATUS_BOOST           (1 << 2)        /* SmartBoost */
#define STATUS_OL              (1 << 3)        /* on line */
#define STATUS_OB              (1 << 4)        /* on battery */
#define STATUS_OVER            (1 << 5)        /* overload */
#define STATUS_LB              (1 << 6)        /* low battery */
#define STATUS_RB              (1 << 7)        /* replace battery */
#define STATUS_BYPASS          (1 << 8)        /* on bypass */
#define STATUS_OFF             (1 << 9)        /* ups is off */
#define STATUS_CHRG            (1 << 10)       /* charging */
#define STATUS_DISCHRG         (1 << 11)       /* discharging */
#define STATUS_HB              (1 << 12)       /* High battery */
#define STATUS_FSD             (1 << 13)       /* Forced Shutdown */

// converts status from char* format (e.g. "OL CHRG") to bitmap representation
AGENT_NUT_EXPORT uint16_t
    upsstatus_to_int (const char *status);

// std::string wrapper for upsstatus_to_int
AGENT_NUT_EXPORT uint16_t
    upsstatus_to_int (const std::string& status);

// converts status from uint16_t bitmap (e.g. STATUS_CHRG|STATUS_OL) to text
// representation (e.g. "OL CHRG")
AGENT_NUT_EXPORT std::string
    upsstatus_to_string (uint16_t status);

// converts status from std::string bitmap (e.g. "12") to text representation
// (e.g. "OL CHRG")
AGENT_NUT_EXPORT std::string
    upsstatus_to_string (const std::string& status);

//  Self test of this class
AGENT_NUT_EXPORT void
    ups_status_test (bool verbose);
//  @end

#endif
