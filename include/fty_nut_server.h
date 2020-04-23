/*  =========================================================================
    fty_nut_server - fty nut actor

    Copyright (C) 2014 - 2020 Eaton                                        
                                                                           
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

#ifndef FTY_NUT_SERVER_H_INCLUDED
#define FTY_NUT_SERVER_H_INCLUDED

#include <czmq.h>

#ifdef __cplusplus
extern "C" {
#endif

//  @interface
//  Create a new fty_nut_server
void fty_nut_server (zsock_t *pipe, void *args);

//  Self test of this class
void fty_nut_server_test (bool verbose);

//  @end

#ifdef __cplusplus
}
#endif

#endif
