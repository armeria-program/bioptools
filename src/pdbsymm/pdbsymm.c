/************************************************************************/
/**

   \file       pdbsymm.c
   
   \version    V1.0
   \date       06.02.17
   \brief      Program to apply non-crystollographic symmetry operations
               from the REMARK 350 data in a PDB file
   
   \copyright  (c) Dr. Andrew C. R. Martin / UCL 2017
   \author     Dr. Andrew C. R. Martin
   \par
               Biomolecular Structure & Modelling Unit,
               Department of Biochemistry & Molecular Biology,
               University College,
               Gower Street,
               London.
               WC1E 6BT.
   \par
               andrew@bioinf.org.uk
               andrew.martin@ucl.ac.uk
               
**************************************************************************

   This code is NOT IN THE PUBLIC DOMAIN, but it may be copied
   according to the conditions laid out in the accompanying file
   COPYING.DOC.

   The code may be modified as required, but any modifications must be
   documented so that the person responsible can be identified.

   The code may not be sold commercially or included as part of a 
   commercial product except as described in the file COPYING.DOC.

**************************************************************************

   Description:
   ============

**************************************************************************

   Usage:
   ======

**************************************************************************

   Revision History:
   =================
-  V1.0  06.02.17 Original

*************************************************************************/
/* Includes
*/
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>

#include "bioplib/MathType.h"
#include "bioplib/SysDefs.h"
#include "bioplib/matrix.h"
#include "bioplib/pdb.h"
#include "bioplib/macros.h"

#define MAXCHAINS 61

/************************************************************************/
/* Defines and macros
*/

/************************************************************************/
/* Globals
*/

/************************************************************************/
/* Prototypes
*/
int main(int argc, char **argv);
void DoRotations(PDB *pdb, ROTLIST *rotlist);
BOOL BuildRotInstruct(ROTLIST **pRotList, char direction, char *amount);
void Usage(void);
BOOL SetupMatrix(int argc, char **argv, REAL matrix[3][3]);


/************************************************************************/
/*>int main(int argc, char **argv)
   -------------------------------
*//**

   Main program for PDB rotation

-  17.06.94 Original    By: ACRM
-  21.07.95 Added -m
-  29.09.97 Added -n
-  22.07.14 Renamed deprecated functions with bl prefix. By: CTP
-  13.02.15 Added whole PDB support   By: ACRM
*/
int main(int argc, char **argv)
{
   FILE     *in      = stdin,
            *out     = stdout;
   WHOLEPDB *wpdb;
   PDB      *pdb;
   BOOL     GotMatrix = FALSE,
            GotRot    = FALSE,
            DoCentre  = TRUE;
   REAL     matrix[3][3];

   argc--;
   argv++;

   GotMatrix = TRUE;
   DoCentre = FALSE;


   /* Handle all switches                                               */
   while(argc)
   {
      if(argv[0][0] == '-')
      {
         switch(argv[0][1])
         {
         case 'h':
            Usage();
            return(0);
         default:
            Usage();
            return(1);
         }
      }
      else
      {
         break;
      }
      
      argc--;
      argv++;
   }

   /* Open input file if specified                                      */
   if(argc)
   {
      if((in=fopen(argv[0],"r"))==NULL)
      {
         fprintf(stderr,"Unable to open input file: %s\n",argv[0]);
         return(1);
      }
      argc--;
      argv++;
   }

   /* Open output file if specified                                     */
   if(argc)
   {
      if((out=fopen(argv[0],"w"))==NULL)
      {
         fprintf(stderr,"pdbrotate: Unable to open output file: %s\n",
                 argv[0]);
         return(1);
      }
      argc--;
      argv++;
   }
   
   /* Read in the PDB file                                              */
   if((wpdb = blReadWholePDB(in))==NULL)
   {
      fprintf(stderr,"pdbrotate: Unable to read from PDB file\n");
      return(1);
   }

   /* Write the initial copy (unmodified)                               */
   blWriteWholePDBHeader(out, wpdb);
   blWritePDB(out, wpdb->pdb);
   
   WriteSymmetryCopies(out, wpdb);

   /* Need to build everything into one PDB linked list and rebuild conect
      data to do this properly
   */
   /* blWriteWholePDBTrailer(out, wpdb); */ 

   return(0);
}




void WriteSymmetryCopies(FILE *out, WHOLEPDB *wpdb)
{
   REAL  matrix[3][3];
   char  chains[MAXCHAIN][8];
   int   nchains;
   VEC3F trans;
   char  lastChainLabel = 'Z';
   char  chainLabel;
   

   chainLabel = GetNextChainLabel(lastChainLabel);


   pdb=wpdb->pdb;

   while((nchains=ReadSymmetryData(wpdb, matrix, trans, chains))!=0)
   {
      if(!IsIdentityMatrix(matrix, trans))
      {
         int chainNum;
         for(chainNum=0; chainNum<nchains; chainNum++)
         {
            ApplyMatrixAndWriteCopy(out, pdb, chains[chainNum], chainLabel, matrix, trans);
            chainLabel = GetNextChainLabel(chainLabel);
         }
      }
   }
}


/*
REMARK 350   BIOMT1   1  1.000000  0.000000  0.000000        0.00000            
12345612341231234511234123456789012345678901234567890123451234567890
%6s   %4d %3x%5s%1d%4d %10.6f    %10.6f    %10.6f    %5x  %10.6f
*/
int ReadSymmetryData(WHOLEPDB *wpdb, REAL matrix[3][3], VEC3F trans, char *chains[MAXCHAINS][8])
{
   static STRINGLIST *sSymOp = NULL;
   static BOOL       called  = FALSE;
   static sChains[MAXCHAINS][8];
   
   /* First call - step to the start of the symmertry operators */
   if((sSymOp == NULL) && !called)
   {
      called = TRUE;
      
      for(sSymOp = wpdb->header; sSymOp != NULL; NEXT(sSymOp))
      {
         if((sSymOp->string != NULL) && 
            !strncmp(sSymOp->string, "REMARK 350 APPLY THE FOLLOWING TO CHAINS:", 41))
         {
            /* We are now pointing to the first symmetry operator */
            break;
         }
      }
   }
   if(sSymOp == NULL)
      return(0);

   /* This is the normal start point for parsing the symmertry operators - we should be
      pointing to a 'REMARK 350 APPLY THE FOLLOWING TO CHAINS:' or to a 'REMARK 350   BIOMT1'
   */
   
   if(sSymOp->string == NULL)
      return(0);
   
   if(!strncmp(sSymOp->string, "REMARK 350 APPLY THE FOLLOWING TO CHAINS:", 41))
   {
      /* Populate the static list of chains */
      chp = sSymOp->string + 41;
      KILLLEADSPACES(chp);
      for(nchains=0; nchains<MAXCHAINS; nchains++)
      {
         if((chp = blGetWord(chp, sChains[nchains], 8)) == NULL)
            break;
      }
      NEXT(sSymOp);
   }
   
   if((sSymOp != NULL) && (sSymOp->string != NULL))
   {
      /* This should not happen! */
      if(strncmp(sSymOp->string, "REMARK 350   BIOMT1", 19))
      {
         return(0);
      }

      /* Read the three REMARK 350 BIOMTx lines */
      for(i=0; i<3; i++)
      {
         if((sSymOp != NULL) && (sSymOp->string != NULL))
         {
            char recordType[8],
                 subType[8];
            int  recordNum,
                 subTypeNum,
                 instanceNum;
            REAL x,y,z,t;
            
            fsscanf(sSymOp->string, "%6s%4d%3x%5s%1d%4d%10.6f%10.6f%10.6f%5x%10.6f",
                    recordType, &recordNum, subType, &subTypeNum,
                    &instanceNum, &x, &y, &z, &t);
            if(!strncmp(recordType, "REMARK", 6) &&
               (recordNum == 350) &&
               !strncmp(subType, "BIOMT", 5))
            {
               matrix[i][0] = x;
               matrix[i][1] = y;
               matrix[i][2] = z;
               trans[i]     = t;
            }
            else
            {
               fprintf(stderr,"Warning (pdbsymm): Unexpected record - %s\n",
                       sSymOp->string);
            }
         }
         NEXT(sSymOp);
      }
   }

   while(strncmp(sSymOp->string, "xxxxx)
   

   
   
}



BOOL IsIdentityMatrix(REAL matrix[3][3], VEC3F trans)
{
   int i, j;
   
   /* Test the matrix */
   for(i=0; i<3; i++)
   {
      for(j=0; j<3; j++)
      {
         if((i==j) && (matrix[i][j] != (REAL)1.0))  /* Diagonal not 1.0 */
         {
            return(FALSE);
         }
         else if(matrix[i][j] != (REAL)0.0)         /* Off-diagonal not 0,0 */
         {
            return(FALSE);
         }
      }
   }

   /* Test the translation vector */
   for(i=0; i<3; i++)
   {
      if(trans[i] != (REAL)0.0)
      {
         return(FALSE);
      }
   }

   return(TRUE);
}


char GetNextChainLabel(char chainLabel)
{
   if((chainLabel >= 'A') && (chainLabel < 'Z'))
   {
      chainLabel++;
   }
   else if(chainLabel == 'Z')
   {
      chainLabel = '1';
   }
   else if((chainLabel >= '1') && (chainLabel < '9'))
   {
      chainLabel++;
   }
   else if(chainLabel == '9')
   {
      chainLabel = 'a';
   }
   else if((chainLabel >= 'a') && (chainLabel <'z'))
   {
      chainLabel++;
   }
   else
   {
      chainLabel = 'A';
      printf(stderr, "Warning (pdbsymm): More than 61 chains so reusing chain names!\n");
   }
   
   return(chainLabel);
}



void ApplyMatrixAndWriteCopy(FILE *out, PDB *pdb, char chain[8], REAL matrix[3][3], VEC3F trans)
{
   PDB *pdb2;
   
   if((pdb2 = blGetPDBChainAsCopy(pdb, chain))!=NULL)
   {
      /* Apply the rotations                                               */
      blApplyMatrixPDB(pdb2, matrix);
      /* And the translation                                               */
      blTranslatePDB(pdb2, trans);
      
      /* Write the new PDB file                                            */
      blWritePDB(out,pdb2);

      FREELIST(pdb2, PDB);
   }
   
}

   
/************************************************************************/
/*>void Usage(void)
   ----------------
*//**

-  06.02.17 Original    By: ACRM
*/
void Usage(void)
{
   fprintf(stderr,"\npdbrotate V1.5 (c) 1994-2015 Andrew C.R. \
Martin, UCL\n");
   fprintf(stderr,"Freely distributable if no profit is made\n\n");
   fprintf(stderr,"Usage: pdbrotate [-m 11 12 13 21 22 23 31 32 33] \
[-h]\n");
   fprintf(stderr,"              [-n] [input.pdb [output.pdb]]\n");
   fprintf(stderr,"       --or--\n");
   fprintf(stderr,"       pdbrotate [-x ang] [-y ang] [-z ang] \
[-h]\n");
   fprintf(stderr,"              [input.pdb [output.pdb]]\n\n");
   fprintf(stderr,"       -m           Specify rotation matrix\n");
   fprintf(stderr,"       -n           Do not move to CofG before \
applying\
 matrix\n");
   fprintf(stderr,"       -x, -y, -z   Specify rotations (in degrees)\n");
   fprintf(stderr,"       -h           This help message\n");
   fprintf(stderr,"I/O is to stdin/stdout if not specified\n\n");
   fprintf(stderr,"Rotates a PDB file using the given rotation matrix \
or using the sequence\n");
   fprintf(stderr,"of specified rotations. All rotations are performed \
around the centre\n");
   fprintf(stderr,"of geometry of the molecule. -x, -y and -z rotations \
are applied in\n");
   fprintf(stderr,"sequence and as many rotations are are required may \
be given.\n\n");
}


