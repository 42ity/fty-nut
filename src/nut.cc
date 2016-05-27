/*  =========================================================================
    nut - agent nut structure

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

/*
@header
    nut - agent nut structure
@discuss
@end
*/

#include "agent_nut_classes.h"

//  Structure of our class

struct _nut_t {
    int filler;     //  Declare class properties here
};


//  --------------------------------------------------------------------------
//  Create a new nut

nut_t *
nut_new (void)
{
    nut_t *self = (nut_t *) zmalloc (sizeof (nut_t));
    assert (self);
    //  Initialize class properties here
    return self;
}


//  --------------------------------------------------------------------------
//  Destroy the nut

void
nut_destroy (nut_t **self_p)
{
    assert (self_p);
    if (*self_p) {
        nut_t *self = *self_p;
        //  Free class properties here
        //  Free object itself
        free (self);
        *self_p = NULL;
    }
}

//  --------------------------------------------------------------------------
//  Self test of this class

void
nut_test (bool verbose)
{
    printf (" * nut: ");

    //  @selftest
    //  Simple create/destroy test
    nut_t *self = nut_new ();
    assert (self);
    nut_destroy (&self);
    //  @end
    printf ("OK\n");
}
