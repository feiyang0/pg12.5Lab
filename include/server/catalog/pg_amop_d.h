/*-------------------------------------------------------------------------
 *
 * pg_amop_d.h
 *    Macro definitions for pg_amop
 *
 * Portions Copyright (c) 1996-2019, PostgreSQL Global Development Group
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 * NOTES
 *  ******************************
 *  *** DO NOT EDIT THIS FILE! ***
 *  ******************************
 *
 *  It has been GENERATED by src/backend/catalog/genbki.pl
 *
 *-------------------------------------------------------------------------
 */
#ifndef PG_AMOP_D_H
#define PG_AMOP_D_H

#define AccessMethodOperatorRelationId 2602

#define Anum_pg_amop_oid 1
#define Anum_pg_amop_amopfamily 2
#define Anum_pg_amop_amoplefttype 3
#define Anum_pg_amop_amoprighttype 4
#define Anum_pg_amop_amopstrategy 5
#define Anum_pg_amop_amoppurpose 6
#define Anum_pg_amop_amopopr 7
#define Anum_pg_amop_amopmethod 8
#define Anum_pg_amop_amopsortfamily 9

#define Natts_pg_amop 9


/* allowed values of amoppurpose: */
#define AMOP_SEARCH		's'		/* operator is for search */
#define AMOP_ORDER		'o'		/* operator is for ordering */


#endif							/* PG_AMOP_D_H */
