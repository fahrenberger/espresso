/** \file lb.c  
 *  Lattice Boltzmann algorithm for solvent degrees of freedom.
 */
#include <mpi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "global.h"
#include "debug.h"
#include "grid.h"
#include "integrate.h"
#include "initialize.h"
#include "interaction_data.h"
#include "particle_data.h"
#include "utils.h"
#include "communication.h"
#include "maggs.h"
#include "thermostat.h"
#include "cells.h"
#include "lb.h"
#include "communication.h" 
#include "domain_decomposition.h"
#include <signal.h>

#define ONE           1.0
#define ZERO          0.0

/* MPI tags for the lb communications: */
/** Tag for communication in CopyPlane */
#define REQ_LB_BCAST    300
#define REQ_LB_SPREAD   301

#define CREEPINGFLOW
#define D3Q18 

/* granularity of particle buffer allocation */
#define PART_INCREMENT  32

/* default parameters which are used in case you do not specify s.th else in the input file, for meaning, cf. itypes.h */

int     defaultgridpoints = 30;
double  defaultrho        = 1.0;  
int     defaultcurrent_type= 0; 
char    defaultcurrentpar[200]= "0.0"; 
double  defaultviscos     = 3.0; 
double  defaultlambda     = (-1.0);  /*  not independent of viscosity, time step and lattice spacing! viscosity overrules lambda */
double  defaultc_sound_sq = 1./2.;     
double  defaulttau        = 0.05;
double  defaultagrid      = 1.;
int     defaultn_veloc    = 18;
int     defaultn_abs_veloc= 2; 
int     defaultn_abs_which[] ={6,12};
T_IVECTOR defaultc_g[18]={   { 1, 0, 0}, 
			     {-1, 0, 0},
			     { 0, 1, 0}, 
			     { 0,-1, 0},
			     { 0, 0, 1}, 
			     { 0, 0,-1},
			     { 1, 1, 0}, 
			     {-1,-1, 0},
			     { 1,-1, 0},
			     {-1, 1, 0},
			     { 1, 0, 1},
			     {-1, 0,-1},
			     { 1, 0,-1},
			     {-1, 0, 1},
			     { 0, 1, 1},
			     { 0,-1,-1},
			     { 0, 1,-1},
			     { 0,-1, 1}};
int  defaultc_abs_sq_g[]={1,1,1,1,1,1,2,2,2,2,2,2,2,2,2,2,2,2};
double  defaultcoef_eq[]={1./12.,1./6.,1./4.,-1./6.,1./24.,1./12.,1./8.,1./12.};

int  defaultranseed        = -123456789;       
char defaultobsfile[]      ="sim";                /* observable file (.bondlen, .msd etc is added) */  
char defaultloadfile[]     ="current_state";      /* configuration file to be loaded (e.g. after crash) */
char defaultnicefile[]      ="sim_state";         /* file for state to be saved to (after equilibration etc.) 
						      includes velocities, forces  and fluid per default */
char defaultbfile[]        ="boundaries.in";
char stopName[200]; 

int      defaultchainlen    = 5000;
int      n_abs_veloc;
double   defaultfriction    = 20.;
double   fluidstep;
double   lambda;
double   viscos;
double   c_sound_sq; 
T_BOOL   defaultboundary   = FALSE;
T_BOOL   fluct;
T_BOOL   siminit = FALSE; 
T_BOOL   boundary1;                        /* yes if there is some other than periodic boundary */
T_BOOL   fixmom = FALSE;                        /* yes if there is some other than periodic boundary */

int*     n_abs_which;
double*  coef_eq;  
int*     c_abs_sq_g;

/*************************** global arrays **********************************/

double*       n     = NULL;                 /* distribution */
double*       n_new = NULL;                 /* distribution to stream to */
double*       rho   = NULL;                 /* mass density */
int*          next  = NULL;               /* next gridpoint address */
T_DVECTOR*    j     = NULL;                 /* current density */
T_DVECTOR*    c_g_d = NULL;                 /* grid vectors of double type */
T_DMATRIX*    pi    = NULL;                 /* stress tensor */

T_IVECTOR*    neighbor = NULL;              /* right neighbor in grid */
T_IVECTOR*    lneighbor = NULL;              /* left neighbor in grid */ 
T_IVECTOR*    c_g=defaultc_g;

T_DVECTOR*    v     = NULL;                 /* monomer velocity */
T_DVECTOR*    force  = NULL;                 /* force on monomer */ 
T_DVECTOR*    xrel  = NULL;                 /* monomer coordinate in unit cell (in lattice units) */
T_GRIDPOS*    xint  = NULL;                 /* lower left front gridpoint of monomer unit cell + PE of this monomer*/ 
T_RANSTORE*   store_rans = NULL;
T_DVECTOR     save_j;


/******************************* global scalars/structs **********************/

int transfer_momentum = 0;
int myNum,myGrpNum,numPes,numGrpPes,myGroup;
T_PLANEINFO planeinfo[6];
MPI_Comm gridComm,groupComm;
MPI_Op MPI_Maxmagnitude;
MPI_Datatype gridMemType,gridFileType,MPI_I32VECTOR;
MPI_Datatype xyPlane,xzPlane,yzPlane;
MPI_Datatype T_MPI_DOUBLE;
MPI_Request request[4];
MPI_Status stat[4];

T_TERM_FLAG terminateFlag = TERM_continue; 

T_IVECTOR nPesPerDir;              /* number of PEs in each Dir */
T_IVECTOR offset;                  /* offset of local box */

LB_structure  compar;                   /* this is the struct where all the parameters of the simulation are stored */

int    counter;                         /* current step after equilibration */ 
int    ranseed;                         /* seed for random number generator */
int    xyzcube;                         /* volume of local box */
int    gridpoints;
int    xsize,ysize,zsize;               /* local size of box */ 
int    xoffset,yoffset,zoffset;         /* local offset of box */
int    n_veloc;                         /* # of grid velocities  */ 
int    how_much_data;                   /* total number of steps */
int    chainlen,nMono;                  /* chainlength and # of chains to be simulated (separately) */
int    gridbegin;                       /* local start of real info (w.o. halo */
int    gridsurface;                     /* surface of loacl box */
int    Npart;        /* number of particles */
double friction,noise;                  /* obvious */
double agrid,tau;                       /* lattice length & time (in LJ units) */
double current_time;                    /* current time (LJ) */
double ampl;                            /* amplitude for stochastic motion */
double deviation=0.;   
double gridpa;
double invgridpa; 

/*****************************************************************************/

void allocmem() {/* allocate memeory */

#if DEBUG
  if (!myNum) printf("Allocating memory \n");
#endif

  c_g_d=(T_DVECTOR*)realloc(c_g_d, n_veloc*sizeof(T_DVECTOR));   
#ifndef D3Q18
  next=(int*)realloc(next, n_veloc*(xyzcube+gridsurface)*sizeof(int));
#endif
  neighbor=(T_IVECTOR*)realloc(neighbor, (gridpoints+2)*sizeof(T_IVECTOR)); /* global */
  lneighbor=(T_IVECTOR*)realloc(lneighbor, (gridpoints+2)*sizeof(T_IVECTOR)); /* global */
  n=(double*)realloc(n, (xyzcube+gridsurface)*n_veloc*sizeof(double));
  j=(T_DVECTOR*)realloc(j, (xyzcube+gridsurface)*sizeof(T_DVECTOR));
  pi=(T_DMATRIX*)realloc(pi, (xyzcube+gridsurface)*sizeof(T_DMATRIX));
  rho=(double*)realloc(rho,(xyzcube+gridsurface)*sizeof(double));
#ifndef D3Q18
  n_new=(double*)realloc(n_new, (xyzcube+gridsurface)*n_veloc*sizeof(double));
#endif

  if ((!j)  || (!rho) || (!c_g_d) || (!n) || (!pi) || (!neighbor) || (!lneighbor)) {
    fprintf(stderr,"*** ERROR in allocmem: couldn't allocate fluid memory\n");
    ExitProgram(TERM_error);
  }
#ifndef D3Q18
  if ((!n_new)  || (!next) ) {
    fprintf(stderr,"*** ERROR in allocmem: couldn't allocate fluid memory\n");
    ExitProgram(TERM_error);
  }
#endif
}

void alloc_part_mem(int npart)
{
  if (npart > Npart) {
    /* round up part + 1 in granularity PART_INCREMENT */
    Npart = PART_INCREMENT*((npart + PART_INCREMENT - 1)/PART_INCREMENT);

    xrel=(T_DVECTOR*)realloc(xrel, Npart*sizeof(T_DVECTOR));
    xint=(T_GRIDPOS*)realloc(xint, Npart*sizeof(T_GRIDPOS));
    v=(T_DVECTOR*)realloc(v, Npart*sizeof(T_DVECTOR));
    force=(T_DVECTOR*)realloc(force, Npart*sizeof(T_DVECTOR));
    store_rans=(T_RANSTORE*)realloc(store_rans, (Npart+MAX_RANREPEATE)*sizeof(T_RANSTORE));

    if ((!v) || (!force) || (!xrel) || (!xint) || (!store_rans))
      {
	fprintf(stderr,"*** ERROR in allocmem: couldn't allocate polymer memory\n");
	ExitProgram(TERM_error);
      }
  }
}


  /*********************************************************************/

void freemem(void){

  if (c_g_d)  free(c_g_d);
  c_g_d = NULL;
  if (n)      free(n);
  n = NULL;
  if (n_new)  free(n_new);
  n_new = NULL;
  if (j)      free(j);
  j = NULL;
  if (rho)    free(rho);
  rho = NULL;
  if (pi)     free(pi);
  pi = NULL;
  if (next)   free(next);
  next = NULL;
  if (neighbor) free(neighbor);
  neighbor = NULL;
  if (lneighbor) free(lneighbor);
  lneighbor = NULL;
  if (xrel)     free(xrel);
  xrel = NULL;
  if (xint)     free(xint);
  xint = NULL;
  if (v)      free(v);
  v = NULL;
  if (force) free(force);
  force = NULL;
}

#define MAXRANDINV (1.0/2147483647.0)
static T_INT32 ibm = 1;

static double mibmran (void) {

  ibm *= 16807;
  return ibm*MAXRANDINV;
}

/*
static int GetLinearIndex (int multiIndex[], int vol[]) {

  int linear = multiIndex[0];
  int i;

  for (i = 1;i < SPACE_DIM;i++) linear += vol[i-1]*multiIndex[i];
  return linear;
}


*/
void ibmset (int iseed) {

  ibm = iseed*3;
}

/*********  intialize inportant variables       **********************/
static void InitCommand() {

    compar.ranseed    = defaultranseed;
    compar.current_type= defaultcurrent_type ;
    strcpy(compar.currentpar,defaultcurrentpar);

    compar.chainlen   = defaultchainlen;  
    compar.boundary   = defaultboundary;
    compar.lambda     = defaultlambda;
    compar.c_sound_sq = defaultc_sound_sq; 

//    compar.agrid      = defaultagrid;       
//    compar.rho        = defaultrho;
//    compar.gridpoints = defaultgridpoints;
//    compar.tau        = defaulttau;  		   
//    compar.friction   = defaultfriction;
//    compar.viscos     = defaultviscos  ;
}							       


void printfields(T_DVECTOR* j,double* rho,T_DMATRIX* pi){

  /**********************************************************
   print hydrodynamic fields rho,j, and pi_xz to stdout
   
   Author: Ahlrichs, 01/10/97
   **********************************************************/
  
  int i,k,x,y,z,gx,gy,gz;
  int xsizep2 = xsize+2;
  int ysizep2 = ysize+2;
  int zsizep2 = zsize+2;

  if (!myNum) printf(" x[a]  y[a]  z[a]         rho           j_x           j_y           j_z         pi_xz\n");
  for (k=0;k<numPes;k++){
    if (k==myNum) {
      for(z=0;z<(zsizep2);z++){
	for (y=0;y<(ysizep2);y++){
	  for (x=0;x<(xsize+2);x++){
	    i = z*xsizep2*ysizep2+y*xsizep2+x;
	    gx = (x-1+offset[0]+gridpoints)%gridpoints;
	    gy = (y-1+offset[1]+gridpoints)%gridpoints;
	    gz = (z-1+offset[2]+gridpoints)%gridpoints;
	    printf("%d,%d: %5d %5d %5d %12.5e %12.5e %12.5e %12.5e %12.5e\n",myGroup,myGrpNum,gx,gy,gz,rho[i],j[i][0],j[i][1],j[i][2],pi[i][2][0]);
	  }
	}
      }
    }
    MPI_Barrier(MPI_COMM_WORLD);
  }
if (!myNum) fflush(stdout);
}
	     

void calcfields(double* n,T_BOOL calc_pi){

  /***********************************************************
   calculate hydrodynamic fields rho und j from distribution n
   also calculate stress tensor if calc_pi

   include halo region (maybe someone wants to use this routine
   for calculations aftwerwards which include halo?)

   Author: Ahlrichs, 01/10/97
   **********************************************************/

  int i,k,m,l,h;
  double help,help2;

#if DEBUG
printf(" %d You're now in CALCFIELDS %d %d \n",this_node,xyzcube,gridsurface);
printf(" And the first grid vector is: %f %f %f\n",c_g_d[0][0],c_g_d[0][1],c_g_d[0][2]);
#endif  
  memset(rho,0,sizeof(double)*(xyzcube+gridsurface));
  memset(j,0,sizeof(T_DVECTOR)*(xyzcube+gridsurface));
  if (calc_pi) memset(pi,0,sizeof(T_DMATRIX)*(xyzcube+gridsurface));


  l=0;

  if (calc_pi){

    for(i=0;i<(xyzcube+gridsurface);i++){
      for(k=0;k<n_veloc;k++){
	help=n[l];
	rho[i]+=help;
	for(m=0;m<3;m++){
	  help2=help*c_g_d[k][m];
	  j[i][m]+=help2;
	  for(h=0;h<=m;h++){
	    pi[i][m][h]+=help2*c_g_d[k][h];
	  }
	}
	l++;
      }
    }
  
  }
  else {
    
#ifdef D3Q18

    for(i=0;i<(xyzcube+gridsurface);i++){

      double* nlocal = &(n[l]);
      rho[i]=nlocal[0]+nlocal[1]+nlocal[2]+nlocal[3]+nlocal[4]+nlocal[5]+
	nlocal[6]+nlocal[7]+nlocal[8]+nlocal[9]+nlocal[10]+nlocal[11]+
	nlocal[12]+nlocal[13]+nlocal[14]+nlocal[15]+nlocal[16]+nlocal[17];
    
      j[i][0] = nlocal[0]-nlocal[1]+nlocal[6]-nlocal[7]+nlocal[8]-nlocal[9]+nlocal[10]-nlocal[11]+nlocal[12]-nlocal[13];
      j[i][1] = nlocal[2]-nlocal[3]+nlocal[6]-nlocal[7]-nlocal[8]+nlocal[9]+nlocal[14]-nlocal[15]+nlocal[16]-nlocal[17];
      j[i][2] = nlocal[4]-nlocal[5]+nlocal[10]-nlocal[11]-nlocal[12]+nlocal[13]+nlocal[14]-nlocal[15]-nlocal[16]+nlocal[17];

      l+=n_veloc;
    }

#else

    for(i=0;i<(xyzcube+gridsurface);i++){
      for(k=0;k<n_veloc;k++){
	help=n[l];
	rho[i]+=help;
	for(m=0;m<3;m++){
	  j[i][m]+=help*c_g_d[k][m];
	}
	l++;
      }      
    }  /* i */
#endif  

  }  /* else */


}

/******************************************************************/

static void calccurrent(int current_type,char* current_par, int gridpoints,T_DVECTOR* j){

/*************************************************************
   calculate initial current field at the gridpoints

   input: currenttype = 0: constant current
                      = 1: linear profile in x-direction 
		           makes use of three dimensions expl.
		       =2: sine profile in x-direction
		           makes use of three dimensions expl.
	  gridpoints

   output: j = current field
           do not include halo region

   Author: Ahlrichs, 26/09/97
   ***********************************************************/

  int i,x,y,z;
  char* dummy;
  double j_0,j_1;   /* parameters for construction of field */ 
  double j_base;

  switch(current_type){
  case 0:    /*  constant current field of strength j_0 in y direction */
    j_0=strtod(current_par,&dummy);
    for (z=1; z<(zsize+1); z++){
    for (y=1; y<(ysize+1); y++){
    for (x=1; x<(xsize+1); x++){
      i = z*(ysize+2)*(xsize+2)+y*(xsize+2)+x;
      j[i][0]=0.0;
      j[i][2]=0.0;
      j[i][1]=j_0;
    }
    }
    }
    break;
  case 1:    /* linear z-velocity profile of max. value j_0 in x-direction */
    j_0=strtod(current_par,&dummy);
    j_base=2.*j_0/(double)gridpoints;
    if (!myNum) printf("assigning linear profile: max. value = %f\n",j_0);
    for (z=1; z<(zsize+1); z++){
    for (y=1; y<(ysize+1); y++){
    for (x=1; x<(xsize+1); x++){
      i = z*(ysize+2)*(xsize+2)+y*(xsize+2)+x;
      if (x+offset[0]<gridpoints/2+1) j[i][2]=j_base*(double)(x+offset[0]-1);
      else j[i][2]=j_0-j_base*(double)(x+xoffset-gridpoints/2);
      j[i][0]=0.;
      j[i][1]=0.;
    }
    }
    }
    break;
  case 2:   /* sine profile for z-velo of max. value j_0 in x-direction, wavenumder 2*pi*j_1/gridpoints */
    j_0=strtod(current_par,&dummy);
    j_1=strtod(dummy,&dummy);
    if (!myNum) printf("Assigning sine profile: Amplitude=%f,  Mode=%f\n",j_0,j_1);
    j_base=2.*PI*j_1/(double)gridpoints;
    for (z=1; z<(zsize+1); z++){
    for (y=1; y<(ysize+1); y++){
    for (x=1; x<(xsize+1); x++){
      i = z*(ysize+2)*(xsize+2)+y*(xsize+2)+x;
      j[i][2]=j_0*sin(j_base*(y+offset[1]-1));
      j[i][0]=0.;
      j[i][1]=0.;
    }
    }
    }
    break;
  default:
    if (!myNum) fprintf(stderr,"*** ERROR in calccurrent: unknown current type (%d)\n",
	    current_type);
    ExitProgram(TERM_error);
    break;
  }

}


/*****************************************************************/

static double vecveccontr_trace(double *a,double *b){

  /***************************************************************
   calculate contraction of two dyadic products of vectors	
   take care of symmetry

   Author: Ahlrichs, 22/10/97
   **************************************************************/

  
  int i,k;
  double sum=0.; 
  double sum2=0.;

  for(i=0;i<3;i++){
    sum2=0.;
    for(k=0;k<3;k++){
      sum2+=a[k]*b[k];
    }
    sum+=sum2*a[i]*b[i];
  }
  sum-=scalar(a,a)*scalar(b,b)/D_SPACE_DIM;
  return sum;
}

/*****************************************************************/

//static double tensveccontr_trace_lin(double t[],T_DVECTOR v){

  /***************************************************************
   calculate contraction of a symmetric tensor and the dyadic 
   product of a vector. Assumes linear storing of the tensor

   Author: Ahlrichs, 22/10/97
   **************************************************************/
/*
  double sum2,trace;

  trace=(t[0]+t[2]+t[5])*SPACE_DIM_INV;
  sum2=(t[0]-trace)*v[0]*v[0]+(t[2]-trace)*v[1]*v[1]+(t[5]-trace)*v[2]*v[2];
  sum2+=2.0*(v[0]*v[1]*t[1]+v[0]*v[2]*t[3]+v[1]*v[2]*t[4]);

  return sum2;
*/
//}

//static double tensveccontr_trace(T_DMATRIX t,T_DVECTOR v){

  /***************************************************************
   calculate contraction of a symmetric tensor and the dyadic 
   product of a vector

   Author: Ahlrichs, 22/10/97
   **************************************************************/
/*

  double sum2,trace;


  trace=(t[0][0]+t[1][1]+t[2][2])*SPACE_DIM_INV;
  sum2=(t[0][0]-trace)*v[0]*v[0]+(t[1][1]-trace)*v[1]*v[1]+(t[2][2]-trace)*v[2]*v[2];
  sum2+=2.0*(v[0]*v[1]*t[1][0]+v[0]*v[2]*t[2][0]+v[1]*v[2]*t[2][1]);

  return sum2;
}

*/
/*****************************************************************/

static void calcn_eq(int* n_abs_which,int* c_abs_sq_g,double* coef_eq,
	      int n_abs_veloc){

  /************************************************************
   calculate equilibrium distribution at grid points from
   local values of rho and j, using equilibrium coefficients (coef_eq)
   do not calculate n_eq in halo region

   n_eq^i=\rho(a_0^c_i+a_1^c_i.j*c_i+a_2^c_i.jj:cc+a_3^c_i.j**2

   Author: Ahlrichs,29/09/97
   ************************************************************/
  
  int i,k,l,off,off2,off3,x,y,z;
  int xsizep2 = xsize+2;
  int ysizep2 = ysize+2;

  for (z = 1; z<(zsize+1); z++){
    for (y = 1; y<(ysize+1); y++){
      for (x = 1; x<(xsize+1); x++){
	i   = z*xsizep2*ysizep2+y*xsizep2+x;    /* index for j,rho (no halo) */
	off = i*n_veloc;                        /* index for n */
	off3= 0;
	for(k=0;k<n_abs_veloc;k++){
	  off2=k*4;
	  for(l=0;l<n_abs_which[k];l++){
	    n[off]=rho[i]*coef_eq[off2]+coef_eq[off2+1]*scalar(j[i],c_g_d[off3])
	      +(coef_eq[off2+2]*vecveccontr_trace(j[i],c_g_d[off3])+coef_eq[off2+3]*
		scalar(j[i],j[i]))/rho[i];
	    off3++;
	    off++;
	  }
	}
      }
    }
  }

#if DEBUG
  if (!myNum) printf("Calculated n_eq, returning to NEW-routine\n");
#endif

}


void new(){

  /***********************************************************
   set up new configuration, allocate memory 
   input:  big structure par 
   output: n[i]            = number density at each site
                             with certain velocity 
	     i=(x+y*gridpoints+z*gridpoints^2)*n_veloc+veloc_now
	   j[i][3] = current field 
	   rho[i]          = density
	   xf,xp,xrefl,xint,v = chain pos. and velo.
	   sets global variables for simulation, allocate memory
	   
   author: Ahlrichs, 26/09/97
   **********************************************************/

  /*define variables  (maybe global variables later) */  

  double   rho_bar=compar.rho;        
  T_IVECTOR* c_g=defaultc_g;
  int      i,k;

#ifdef DB
  if (!myNum) printf("You're now in routine NEW\n");
#endif

  /* initialize global variables, allocate memory */

  gridpoints = compar.gridpoints;
  chainlen   = compar.chainlen;
  n_veloc    = defaultn_veloc; 
  boundary1   = compar.boundary;
  if (boundary1)  { if(!myNum) fprintf(stderr,"ERROR in NEW: boundaries are specified but not included in this version\n"); ExitProgram(TERM_error);}
  agrid      = compar.agrid;
  tau        = compar.tau;
  ranseed    = compar.ranseed;
  gridpa     = gridpoints*agrid;
  invgridpa  = 1.0/gridpa;

  nPesPerDir[0] = node_grid[2]; 
  nPesPerDir[1] = node_grid[1];
  nPesPerDir[2] = node_grid[0];
  xsize = gridpoints/nPesPerDir[0];   
  ysize = gridpoints/nPesPerDir[1];
  zsize = gridpoints/nPesPerDir[2];
  offset[0] = xsize*node_pos[2];
  offset[1] = ysize*node_pos[1];
  offset[2] = zsize*node_pos[0];

  xyzcube    = xsize*ysize*zsize;
  gridbegin  = (xsize+2)+(xsize+2)*(ysize+2)+1;       /* halo */
  gridsurface= (xsize+2)*(ysize+2)*(zsize+2)-xyzcube; /* halo */
  

  allocmem();
  alloc_part_mem(1);

  counter=0;
  current_time=0.0;
  
  ibmset(abs((2*myGroup+1)*ranseed));

  for(k=0;k<n_veloc;k++){                                        /* calculate double type lattice vector */
    for(i=0;i<3;i++) c_g_d[k][i]=(double)c_g[k][i];
  }

  /* setup the fields according to input parameters */

  calccurrent(compar.current_type,compar.currentpar,gridpoints,j);   /* current field setup */
  for(i=0;i<xyzcube+gridsurface;i++) rho[i]=rho_bar*agrid*agrid*agrid/MASSRATIO;   /*  density setup + unit change */ 

#if DEBUG
  if (!myNum) printf("Assigning density to constant value %f\n",rho_bar);
#endif

  memset(n,0,(xyzcube+gridsurface)*n_veloc*sizeof(double));
  calcn_eq(defaultn_abs_which,defaultc_abs_sq_g,defaultcoef_eq, defaultn_abs_veloc);       /* calc. eq. distribution */


  halo_init();
  halo_update();

  calcfields(n,TRUE);

#ifdef DEBUG2
  {  /* write input rho and j, compare with result from n_eq */
    printfields(j,rho,pi);
  }
#endif

  
} /*********************************************************
   end of NEW							    
   *********************************************************/


#define IM1 2147483563
#define IM2 2147483399
#define AM1 (1.0/IM1)
#define IMM1 (IM1-1)
#define IA1 40014
#define IA2 40692
#define IQ1 53668
#define IQ2 52774
#define IR1 12211
#define IR2 3791
#define NTAB 32
#define NDIV1 (1+IMM1/NTAB)
#define EPS 1.2e-7
//#define RNMX (1.0-EPS)

double ran2(int *idum){
  
  int j;
  int k;
  static int idum2=123456789;
  static int iy=0;
  static int iv[NTAB];
  double temp;

  if (*idum<=0){
    if (-(*idum)<1) *idum=1;
    else *idum=-(*idum);
    idum2=(*idum);
    for(j=NTAB+7;j>=0;j--){
      k=(*idum)/IQ1;
      *idum=IA1*(*idum-k*IQ1)-k*IR1;
      if (*idum<0) *idum+=IM1;
      if (j<NTAB) iv[j]=*idum;
    }
    iy=iv[0];
  }
  k=(*idum)/IQ1;
  *idum=IA1*(*idum-k*IQ1)-k*IR1;
  if (*idum<0) *idum+=IM1;
  k=idum2/IQ2;
  idum2=IA2*(idum2-k*IQ2)-k*IR2;
  if (idum2<0) idum2+=IM2;
  j=iy/NDIV1;
  iy=iv[j]-idum2;
  iv[j]=*idum;
  if (iy<1) iy+=IMM1;
  if((temp=AM1*iy)>RNMX) return RNMX;
  else {
    /*    printf("RAN2 returns %f\n",temp);*/
    return (temp);
  }
}

#define MBIG 1000000000
#define MSEED 161803398
#define MZ 0
#define FAC (1.0/MBIG)

void ran3(int *idum,double *ranstore, int how_many)
{
	static int inext,inextp;
	static int ma[56];
	static int iff=0;
	int mj,mk;
	int i,ii,k;

	if (*idum < 0 || iff == 0) {
		iff=1;
		mj=MSEED-(*idum < 0 ? -*idum : *idum);
		mj %= MBIG;
		ma[55]=mj;
		mk=1;
		for (i=1;i<=54;i++) {
			ii=(21*i) % 55;
			ma[ii]=mk;
			mk=mj-mk;
			if (mk < MZ) mk += MBIG;
			mj=ma[ii];
		}
		for (k=1;k<=4;k++)
			for (i=1;i<=55;i++) {
				ma[i] -= ma[1+(i+30) % 55];
				if (ma[i] < MZ) ma[i] += MBIG;
			}
		inext=0;
		inextp=31;
		*idum=1;
	}
	for (k=0;k<how_many;k++){
	  if (++inext == 56) inext=1;
	  if (++inextp == 56) inextp=1;
	  mj=ma[inext]-ma[inextp];
	  if (mj < MZ) mj += MBIG;
	  ma[inext]=mj;
	  ranstore[k]=2.0*mj*FAC-1.0;
	}
}
#undef MBIG
#undef MSEED
#undef MZ
#undef FAC
  
/**************************************************************************/
/**************************************************************************/
/**************************************************************************/
/**************************************************************************/
/**************************************************************************/
/**************************************************************************/
/**************************************************************************/
/**************************************************************************/


static void propagate_n(){ 

  /*************************************************************
   propagate the discrete velocity distribution n according to the
   grid vectors c_g to position at next time step
   optimized if gridvectors are explicitly known
   
   this needs a correct hal region, which is destroyed during the
   shifting process.

   Author: Ahlrichs, 23/10/97
   last change: Ahlrichs, 01/05/99
   ************************************************************/
  
#ifndef D3Q18
  int i;

  for(i=0;i<(xyzsurface+gridsurface)*n_veloc;i++){
    n_new[next[i]] = n[i];
  }

#else

  int l;
  int yperiod = xsize+2;  /* include halo regions */
  int zperiod = (ysize+2)*(xsize+2);
  int next_0 = n_veloc;   /* index shifts for the 18 velocities */
  int next_1 =-n_veloc+1;
  int next_2 = n_veloc*yperiod+2;
  int next_3 =-n_veloc*yperiod+3;
  int next_4 = n_veloc*zperiod+4;
  int next_5 =-n_veloc*zperiod+5;
  int next_6 = n_veloc*(yperiod+1)+6;
  int next_7 =-n_veloc*(yperiod+1)+7;
  int next_8 =-n_veloc*(yperiod-1)+8;
  int next_9 = n_veloc*(yperiod-1)+9;
  int next_10= n_veloc*(zperiod+1)+10;
  int next_11=-n_veloc*(zperiod+1)+11;
  int next_12=-n_veloc*(zperiod-1)+12;
  int next_13= n_veloc*(zperiod-1)+13;
  int next_14= n_veloc*(zperiod+yperiod)+14;
  int next_15=-n_veloc*(zperiod+yperiod)+15;
  int next_16=-n_veloc*(zperiod-yperiod)+16;
  int next_17= n_veloc*(zperiod-yperiod)+17;
  int tmpbegin,tmpend;
  
  tmpbegin = (xyzcube+gridsurface-gridbegin)*n_veloc;
  for (l = tmpbegin;l>-1;l-=n_veloc){  /* top down for upgoing velocities */
    n[l+next_0] = n[l];    /* (1,0,0) */
    n[l+next_2] = n[l+2];  /* (0,1,0) */
    n[l+next_4] = n[l+4];  /* (0,0,1) */
    n[l+next_6] = n[l+6];  /* (1,1,0) */
    n[l+next_9] = n[l+9];  /* (-1,1,0) */
    n[l+next_10] = n[l+10];/* (1,0,1) */
    n[l+next_13] = n[l+13];/* (-1,0,1) */
    n[l+next_14] = n[l+14];/* (0,1,1) */
    n[l+next_17] = n[l+17];/* (0,-1,1) */
  }
  
  tmpbegin = gridbegin*n_veloc;
  tmpend = (xyzcube+gridsurface)*n_veloc;
  for (l=tmpbegin;l<tmpend;l+=n_veloc){  /* bottom up for downgoing velocities */
    n[l+next_1] = n[l+1];  /* (-1,0,0) */
    n[l+next_3] = n[l+3];  /* (0,-1,0) */
    n[l+next_5] = n[l+5];  /* (0,0,-1) */
    n[l+next_7] = n[l+7];  /* (-1,-1,0) */
    n[l+next_8] = n[l+8];  /* (1,-1,0) */
    n[l+next_11] = n[l+11];/* (-1,0,-1) */
    n[l+next_12] = n[l+12];/* (1,0,-1) */
    n[l+next_15] = n[l+15];/* (0,-1,-1) */
    n[l+next_16] = n[l+16];/* (0,1,-1) */
  }
#endif

}


/**************************************************************/

static void add_fluct(double lambda,double rho,double pi_lin[]){


  /**************************************************************
   calculate fluctuations in stress tensor by random contribution
   to it. The amplitude of the fluctuations is given by a discrete
   analogon to the fluctuation-dissipation theorem. By this 
   method fluctuations in the current arise while keeping momentum
   conservation alive. Only transverse modes! (incompressible limit) 
   
   cf. Ladd for formulas
   
   One velocity vector c_i_d, its second eq. coeff. and its magnitude 
   are input; for amplitude temperature, lambda and density 
   are needed. Fluctuations are added to pi.
   

   Author: Ahlrichs, 06/11/97
   **************************************************************/

  double rootrho=sqrt(rho);                                      
  int k,l,o,n;
  double help,x;
  double trace=0.;
  double ranstore[6];  /* 6 = 3*(3+1)/2 */

  ran3(&ranseed,ranstore,6);

  o=0; n=0;
  for(k=0;k<3;k++){
    x=ranstore[n++];
    help=SQRT2*x*ampl*rootrho;
    trace+=help;
    for(l=0;l<k;l++){
      x=ranstore[n++];
      pi_lin[o]-=x*ampl*rootrho;
      o++;
    }
    pi_lin[o]-=help;
    o++;
  }

  trace*=SPACE_DIM_INV;
  pi_lin[0]+=trace;
  pi_lin[2]+=trace;
  pi_lin[5]+=trace;

}


/**********************************************************/
 
static void calc_collis(int n_abs_veloc,
		 int* n_abs_which, double* coef_eq, int* c_abs_sq_g, 
		 double c_sound_sq, double lambda,T_BOOL fluct){


  /******************************************************************
   calculate contribution of collisions at each gridpoint
   this is a totally local affair -> parallelising easy
   collisions enter only through stress tensor, for formulas cf. Ladd
   ifdef D3Q18 uses values of defaultc_g as in above array explicitly, 
               thereby saving time for multipl. with zero, one  
   ifdef CREEPINGFLOW neglects non-linear term in Navier-Stokes eq. (cf. Ladd)

   Author: Ahlrichs, 06/11/97						       
   Last Change: Ahlrichs, 29/03/98

   *******************************************************************/


  static int failcounter = 0;   /* counts the number of discarded randoms ONLY for D3Q18 */
  T_BOOL badrandoms;              /* tells if randoms lead to negative n's ONLY for D3Q18 */

  double   local_rho,rhoc_sq;
  T_DVECTOR  local_j;

#ifdef D3Q18
//  int* nextpoi = next;        
  double* nlocal;
  double saven[18];
  double save_pi_lin[3*(3+1)/2];
  int savel;
  int yperiod     = xsize+2;
  int zperiod     = (xsize+2)*(ysize+2);
#else
  double   nl,help2;
  double   A_eq,B_eq,C_eq,D_eq;
  double   *dumpoi;
#endif

  int      k,l,o,x,y,z;
  double   local_pi_lin[3*(3+1)/2];  

  l=0;
  for (z=1;z<zsize+1;z++){    /* go through all gridpoints except halo*/
    for (y=1;y<ysize+1;y++){
      for (x=1;x<xsize+1;x++){

	l = (x+yperiod*y+zperiod*z)*n_veloc;

    /* first calculate local rho, j and pi */

#ifdef D3Q18

    nlocal=&(n[l]);

    local_rho = 0.0;            
    for (k=0;k<n_veloc;k+=6){ 
      local_rho+=nlocal[k]+nlocal[k+1]+nlocal[k+2]+nlocal[k+3]+nlocal[k+4]+nlocal[k+5];
    }
    
    local_j[0] = nlocal[0]-nlocal[1]+nlocal[6]-nlocal[7]+nlocal[8]-nlocal[9]+nlocal[10]-nlocal[11]+nlocal[12]-nlocal[13];
    local_pi_lin[0] = nlocal[0]+nlocal[1]+nlocal[6]+nlocal[7]+nlocal[8]+nlocal[9]+nlocal[10]+nlocal[11]+nlocal[12]+nlocal[13];

    local_j[1] = nlocal[2]-nlocal[3]+nlocal[6]-nlocal[7]-nlocal[8]+nlocal[9]+nlocal[14]-nlocal[15]+nlocal[16]-nlocal[17];
    local_pi_lin[2] = nlocal[2]+nlocal[3]+nlocal[6]+nlocal[7]+nlocal[8]+nlocal[9]+nlocal[14]+nlocal[15]+nlocal[16]+nlocal[17];

    local_j[2] = nlocal[4]-nlocal[5]+nlocal[10]-nlocal[11]-nlocal[12]+nlocal[13]+nlocal[14]-nlocal[15]-nlocal[16]+nlocal[17];
    local_pi_lin[5] = nlocal[4]+nlocal[5]+nlocal[10]+nlocal[11]+nlocal[12]+nlocal[13]+nlocal[14]+nlocal[15]+nlocal[16]+nlocal[17];

    local_pi_lin[1] = nlocal[6]+nlocal[7]-nlocal[8]-nlocal[9];   /* take a look at defaultc_g and you'll understand this ... */
    local_pi_lin[3] = nlocal[10]+nlocal[11]-nlocal[12]-nlocal[13];
    local_pi_lin[4] = nlocal[14]+nlocal[15]-nlocal[16]-nlocal[17];

#else

    local_rho=0.;     
    

    for(m=0;m<3;m++) {
      local_j[m]=0.0;
    }
    for(o=0;o<max_lin;o++){
	local_pi_lin[o]=0.0;
    }

    for(k=0;k<n_veloc;k++){                      
      nl=n[l];
      local_rho+=nl;
      o=0;
      for(m=0;m<3;m++){
	help2=nl*c_g_d[k][m];
	local_j[m]+=help2;
	for(h=0;h<=m;h++){
	  local_pi_lin[o]+=help2*c_g_d[k][h];
	  o++;
	}
      }
      l++;
    } 
    l-=n_veloc;
#endif

    /* update stress tensor */

#ifdef CREEPINGFLOW

#ifdef D3Q18

    {
      double onepluslambda = 1.0 + lambda;
      rhoc_sq = local_rho*c_sound_sq;
      local_pi_lin[0] = onepluslambda*(local_pi_lin[0]-rhoc_sq);
      local_pi_lin[2] = onepluslambda*(local_pi_lin[2]-rhoc_sq);
      local_pi_lin[5] = onepluslambda*(local_pi_lin[5]-rhoc_sq);
      local_pi_lin[1] = onepluslambda*local_pi_lin[1];
      local_pi_lin[3] = onepluslambda*local_pi_lin[3];
      local_pi_lin[4] = onepluslambda*local_pi_lin[4];
    }

#else
    o=0;
    rhoc_sq=local_rho*c_sound_sq;                     
    for(m=0;m<3;m++) {
      for(h=0;h<m;h++){
	local_pi_lin[o]=(1.+lambda)*(local_pi_lin[o]);
	o++;
      }
      local_pi_lin[o]=rhoc_sq+(1.+lambda)*(local_pi_lin[o]-rhoc_sq);
      o++;
    }

#endif

#else
      
    rhoc_sq=local_rho*c_sound_sq;  
    trace_eq=0.;
    for(m=0;m<3;m++) {
      help=rhoc_sq;
      help2=local_j[m]/local_rho;          
      help=local_j[m]*help2+rhoc_sq; 
      local_pi[m][m]=help+(1.+lambda)*(local_pi[m][m]-help);
      trace_eq+=help;
      for(h=0;h<m;h++){  
	help=local_j[h]*help2;
	local_pi[m][h]=help+(1.+lambda)*(local_pi[m][h]-help);
      }
    }

#endif

            /* update velocity distribution at one gridpoint */

#ifdef D3Q18

    save_pi_lin[0] =  local_pi_lin[0];
    save_pi_lin[1] =  local_pi_lin[1];
    save_pi_lin[2] =  local_pi_lin[2];
    save_pi_lin[3] =  local_pi_lin[3];
    save_pi_lin[4] =  local_pi_lin[4];
    save_pi_lin[5] =  local_pi_lin[5];
    savel = l;    
    for (o=0;o<n_veloc;){
      saven[o++] = n[l++];
      saven[o++] = n[l++];
      saven[o++] = n[l++];
      saven[o++] = n[l++];
      saven[o++] = n[l++];
      saven[o++] = n[l++];
    }
    l=savel;

    if (fluct) add_fluct(lambda,local_rho,local_pi_lin);         /* add random fluctuations to stress tensor */


    do {           /* add fluctuations only if they do not lead to neg. n's */
      

      badrandoms = FALSE;

      {

	double local_rho_times_coeff = local_rho*0.08333333333333333;
	double trace = (local_pi_lin[0]+local_pi_lin[2]+local_pi_lin[5])*SPACE_DIM_INV;
	double help1,help2;

	n[l] = local_rho_times_coeff + local_j[0]*0.16666666666666666 + 0.25*(local_pi_lin[0]-trace);
	if (n[l]<=0.0) { 
	  failcounter ++; badrandoms = TRUE;
	}
	l++;
	n[l] = local_rho_times_coeff - local_j[0]*0.16666666666666666 + 0.25*(local_pi_lin[0]-trace);
	if (n[l]<=0.0) { 
	  failcounter ++; badrandoms = TRUE;
	}
	l++; 
	n[l] = local_rho_times_coeff + local_j[1]*0.16666666666666666 + 0.25*(local_pi_lin[2]-trace);
	if (n[l]<=0.0) { 
	  failcounter ++; badrandoms = TRUE;
	}
	l++; 
	n[l] = local_rho_times_coeff - local_j[1]*0.16666666666666666 + 0.25*(local_pi_lin[2]-trace);
	if (n[l]<=0.0) { 
	  failcounter ++; badrandoms = TRUE;
	}
	l++; 
	n[l] = local_rho_times_coeff + local_j[2]*0.16666666666666666 + 0.25*(local_pi_lin[5]-trace);
	if (n[l]<=0.0) { 
	  failcounter ++; badrandoms = TRUE;
	}
	l++; 
	n[l] = local_rho_times_coeff - local_j[2]*0.16666666666666666 + 0.25*(local_pi_lin[5]-trace);
	if (n[l]<=0.0) { 
	  failcounter ++; badrandoms = TRUE;
	}
	l++; 

#ifndef CREEPINGFLOW
	l-=6;
	for(h=0;h<6;h++){
	  n[l] += -0.16666666666666666*(trace_eq-3.*rhoc_sq);
	  l++;
	} 
#endif

	local_rho_times_coeff = local_rho*0.04166666666666666;
   
	help1 = local_pi_lin[0]-trace+local_pi_lin[2]-trace;
	help2 = local_pi_lin[1]+local_pi_lin[1];
	
	n[l] = local_rho_times_coeff + (local_j[0]+local_j[1])*0.08333333333333333+ 0.125*(help1+help2);
	if (n[l]<=0.0) { 
	  failcounter ++; badrandoms = TRUE;
	}
	l++; 
	n[l] = local_rho_times_coeff - (local_j[0]+local_j[1])*0.08333333333333333+ 0.125*(help1+help2);
	if (n[l]<=0.0) { 
	  failcounter ++; badrandoms = TRUE;
	}
	l++; 
	n[l] = local_rho_times_coeff + (local_j[0]-local_j[1])*0.08333333333333333+ 0.125*(help1-help2);
	if (n[l]<=0.0) { 
	  failcounter ++; badrandoms = TRUE;
	}
	l++; 
	n[l] = local_rho_times_coeff - (local_j[0]-local_j[1])*0.08333333333333333+ 0.125*(help1-help2); 	
	if (n[l]<=0.0) { 
	  failcounter ++; badrandoms = TRUE;
	}
	l++; 

	help1 = local_pi_lin[0]-trace+local_pi_lin[5]-trace;
	help2 = local_pi_lin[3]+local_pi_lin[3];

	n[l] = local_rho_times_coeff + (local_j[0]+local_j[2])*0.08333333333333333+ 0.125*(help1+help2);
	if (n[l]<=0.0) { 
	  failcounter ++; badrandoms = TRUE;
	}
	l++; 
	n[l] = local_rho_times_coeff - (local_j[0]+local_j[2])*0.08333333333333333+ 0.125*(help1+help2);
	if (n[l]<=0.0) { 
	  failcounter ++; badrandoms = TRUE;
	}
	l++; 
	n[l] = local_rho_times_coeff + (local_j[0]-local_j[2])*0.08333333333333333+ 0.125*(help1-help2);
	if (n[l]<=0.0) { 
	  failcounter ++; badrandoms = TRUE;
	}
	l++; 
	n[l]  = local_rho_times_coeff - (local_j[0]-local_j[2])*0.08333333333333333+ 0.125*(help1-help2); 
	if (n[l]<=0.0) { 
	  failcounter ++; badrandoms = TRUE;
	}
	l++; 

	help1 = local_pi_lin[2]-trace+local_pi_lin[5]-trace;
	help2 = local_pi_lin[4]+local_pi_lin[4];

	n[l]  = local_rho_times_coeff + (local_j[1]+local_j[2])*0.08333333333333333+ 0.125*(help1+help2); 
	if (n[l]<=0.0) { 
	  failcounter ++; badrandoms = TRUE;
	}
	l++; 
	n[l] = local_rho_times_coeff - (local_j[1]+local_j[2])*0.08333333333333333+ 0.125*(help1+help2);
	if (n[l]<=0.0) { 
	  failcounter ++; badrandoms = TRUE;
	}
	l++; 
	n[l] = local_rho_times_coeff + (local_j[1]-local_j[2])*0.08333333333333333+ 0.125*(help1-help2);  
	if (n[l]<=0.0) { 
	  failcounter ++; badrandoms = TRUE;
	}
	l++; 
	n[l] = local_rho_times_coeff - (local_j[1]-local_j[2])*0.08333333333333333+ 0.125*(help1-help2);
	if (n[l]<=0.0) { 
	  failcounter ++; badrandoms = TRUE;
	}
	l++; 
	

#ifndef CREEPINGFLOW
	l-=12;
	for(h=6;h<n_veloc;h++){
	  n[l]+=  0.08333333333333333*(trace_eq-3.*rhoc_sq);
	  l++;
	} 
#endif

	
      }

      if (badrandoms) {
	
	if (fluct) {  /* recover old values of l, the stress tensor and n */
	  if (failcounter%100==0) printf("Warning: PE %2d:n negative (calc_collis)=>new randoms (time: %f, failcounter: %d)\n",myNum,current_time,failcounter); 
	  local_pi_lin[0] =  save_pi_lin[0];
	  local_pi_lin[1] =  save_pi_lin[1];
	  local_pi_lin[2] =  save_pi_lin[2];
	  local_pi_lin[3] =  save_pi_lin[3];
	  local_pi_lin[4] =  save_pi_lin[4];
	  local_pi_lin[5] =  save_pi_lin[5];
	  l = savel;
	  for (o=0;o<n_veloc;){
	    n[l++]=saven[o++];
	    n[l++]=saven[o++];
	    n[l++]=saven[o++];
	    n[l++]=saven[o++];
	    n[l++]=saven[o++];
	    n[l++]=saven[o++];
	  }
	  l=savel;
 
	  add_fluct(lambda,local_rho,local_pi_lin);         /* try new random fluctuations to stress tensor */
	}
	else {
	  fprintf(stderr,"ERROR in calc_collis: n is getting negative and no random numbers are present (time: %f)\n",current_time);
	  ExitProgram(TERM_error);
	}
      }

    } while (badrandoms);

#else   /* = !D3Q18 */

  l-=n_veloc;   /* update velocity distribution at one gridpoint */
  
  if (fluct) add_fluct(lambda,local_rho,local_pi_lin);         /* add random fluctuations to stress tensor */

  off3=0;
    for(k=0;k<n_abs_veloc;k++){
      dumpoi = &(coef_eq[k*4]);
      A_eq = *dumpoi;
      B_eq = *(++dumpoi);
      C_eq = *(++dumpoi);
#ifndef CREEPINGFLOW
      D_eq = *(++dumpoi);
#endif
      for(h=0;h<n_abs_which[k];h++){
	n[l] = local_rho*A_eq+scalar(local_j,c_g_d[off3])*B_eq+tensveccontr_trace_lin(local_pi_lin,c_g_d[off3])*C_eq;
#ifndef CREEPINGFLOW
	 n[l]+=D_eq*(trace_eq-3.*rhoc_sq);
#endif
	 off3++;
	 l++;
      }
    }
 
#endif

      }
    } 
  }  /* end loop over all gridpoints */

}

			    
/*************************************************************/

//static void calc_fourier(char* rawfilename){

  /*****************************************************
   ** calculate fourier transform of current density 
   ** and write it to file (append)  
   **
   ** Author: Ahlrichs, 02/26/98
   **
   *****************************************************/

/*
         double  arg,sinargi,cosargi;
         int     ikx,i,ix,x,y,z;
  static double fourier_x[MAX_FOURIER][2], fourier_y[MAX_FOURIER][2], fourier_z[MAX_FOURIER][2];
  static FILE     *f;
  static char    fourier_name[200];
  static double  kmin;
  static int     nx;
  static double  deltak;
  static T_BOOL    virginity=TRUE;  
  

  if (counter>=0){

    if (virginity) {
      virginity=FALSE;
      printf("here we go!\n");
      strcpy(fourier_name,rawfilename);
      strcat(fourier_name,".fourier");
      printf("fourier_name=%s",fourier_name);
      kmin=0.0;
      nx=30;
      deltak=PI/(double)nx;
    }

    if (counter==0) {
      printf("u should not be here when loading");
    f=fopen(fourier_name,"w");
    fclose(f);
    }

    memset(fourier_x,0,sizeof(fourier_x));
    memset(fourier_y,0,sizeof(fourier_y));
    memset(fourier_z,0,sizeof(fourier_z));

    f=fopen(fourier_name,"a");
    
    for (ikx = 0;ikx < nx;ikx++) {      
      arg=kmin+deltak*ikx;
      for (z=1;z<zsize+1;z++){
	for (y=1;y<ysize+1;y++){
	  for (x=1;x<xsize+1;x++){
	    i = z*(xsize+2)*(ysize+2)+y*(xsize+2)+x; 
	    ix=x-1;
	    sinargi=sin(arg*ix);
	    cosargi=cos(arg*ix);
	    fourier_x[ikx][0] += j[i][0]*sinargi;
	    fourier_x[ikx][1] += j[i][0]*cosargi;
	    fourier_y[ikx][0] += j[i][1]*sinargi;
	    fourier_y[ikx][1] += j[i][1]*cosargi;
	    fourier_z[ikx][0] += j[i][2]*sinargi;
	    fourier_z[ikx][1] += j[i][2]*cosargi;
	  }
	}
      }

      
      fprintf(f,"%f  %f  %11.4e  %11.4e  %11.4e  %11.4e  %11.4e  %11.4e\n",current_time,arg,fourier_x[ikx][0],fourier_x[ikx][1],fourier_y[ikx][0],fourier_y[ikx][1],fourier_z[ikx][0],fourier_z[ikx][1]);
    }
    
    fclose(f);

  }

}
*/
void print_status(char* fromroutine){

  printf("--------------------------------------------------------\n");
  printf("| status report:\n");
  printf("| --------------\n");
  printf("| current routine: %s\n",fromroutine);
  printf("| current LJ-time: %f\n",current_time);
  printf("| current    step: %d\n\n",counter);
  printf("| LB simulation parameters:\n");
  printf("| -------------------------\n");
  printf("| lamdba       = %f\n",compar.lambda);
  printf("| gridpoints   = %d\n",gridpoints);
  printf("| a_grid(LJ)   = %f\n",agrid);
  printf("| t_grid(LJ)   = %f\n",tau);
  printf("| temp(LJ)     = %f\n",temperature);
  if (temperature<BOTT) printf("|     => no fluctuations included\n\n");
  else printf("|    => amplitude of stoch. term (LB) = %f\n\n",ampl);
  if (boundary1){
    printf("| static boundary included\n");
    printf("| ------------------------\n");
  }
  else {
    printf("| periodic boundary conditions\n");
    printf("| ----------------------------\n");
  }
  printf("| technical details:\n\n");
  printf("| ranseed      = %d\n",ranseed);
  printf("--------------------------------------------------------\n");

}


/**********************************************************************/


void prerun(){ 

  /*********************************************************
   run the simulation with the initial value n of the discrete 
   velocity distribution function. Two main parts for one time step:
     1. calculate collisions
     2. propagate ditribution on lattice


   Author: Ahlrichs, 30/10/97; Lobaskin 10/09/04
   *********************************************************/

  double   viscos=compar.viscos;
  double   dummy[1];
  int i,k;
  
#ifdef DEBUG
  if (!myNum) printf("You're now in routine PRERUN\n");
#endif
  /* initialization of global variables, unit transfers to grid units for fluid part */
  agrid     = compar.agrid;
  tau       = compar.tau;

  /* derived quantities for particles stuff */

  gridpa    = agrid*gridpoints;
  invgridpa = 1.0/gridpa; 

  c_sound_sq = compar.c_sound_sq; 
  n_abs_veloc= defaultn_abs_veloc;
  n_abs_which= defaultn_abs_which;
  c_g        = defaultc_g;
  coef_eq    = defaultcoef_eq;  
  c_abs_sq_g = defaultc_abs_sq_g;

  if (compar.gridpoints!= gridpoints) {
    if (!myNum) fprintf(stderr,"*** ERROR in RUN: You are not allowed to change gridpoints in this stage");
    ExitProgram(TERM_error);
  }
  n_veloc    = defaultn_veloc; 

  friction   = compar.friction;
  noise      = sqrt(6.0*friction*temperature/time_step);

  /*  if ((defaultlambda-lambda)<BOTT){ lambda overrules viscosity as input par. */
    lambda=-2./(6.*viscos*tau/(agrid*agrid)+1.);
    compar.lambda=lambda;

#ifdef DB
    printf("tau %f viscos %f agrid %f lambda %f\n",tau,viscos,agrid,lambda);
    if (!myNum ) printf("prerun: lambda changed according to input viscosity new value: lambda = %f\n",lambda);
#endif        
    /*  }*/
  
  if (temperature>BOTT) {  /* fluctuating hydrodynamics ? */
    fluct=TRUE; 
    ampl=sqrt(3.*2.*(-1./6.)*(2./lambda+1.)*temperature*lambda*lambda*tau*tau/(agrid*agrid*agrid*agrid*MASSRATIO)); 
                                /* amplitude for stochastic part, cf. Ladd + unit conversion */
                                /* caveat:3 comes from the fact */       
                                /* that random numbers must have variance 1  */ 
                                /* multiplication with rho is done with local value in add_fluct */
    
  }
  else fluct=FALSE;
  
  for(k=0;k<n_veloc;k++){             
    for(i=0;i<3;i++) c_g_d[k][i]=(double)c_g[k][i];
  }

    for(k=0;k<xsize;k++) {
      neighbor[k][0]=k+1;
      lneighbor[k+1][0]=k;
    }
    if (nPesPerDir[0]==1) {
      neighbor[xsize][0]=1; 
      lneighbor[1][0]=xsize;
    }
    else {
      neighbor[xsize][0]=xsize+1;
      lneighbor[1][0]=0;
    }
    for(k=0;k<ysize;k++) {
      neighbor[k][1]=k+1;
      lneighbor[k+1][1]=k;
    }
    if (nPesPerDir[1]==1) {
      neighbor[ysize][1]=1; 
      lneighbor[1][1]=ysize;
    }
    else {
      neighbor[ysize][1]=ysize+1;
      lneighbor[1][1]=0;
    }
    for(k=0;k<zsize;k++) {
      neighbor[k][2]=k+1;
      lneighbor[k+1][2]=k;
    }
    if (nPesPerDir[2]==1) {
      neighbor[zsize][2]=1; 
      lneighbor[1][2]=zsize;
    }
    else {
      neighbor[zsize][2]=zsize+1;
      lneighbor[1][2]=0;
    }    
    lneighbor[0][0]=-1;   /* undefined indices get a -1 to promote segmentation faults */
    lneighbor[0][1]=-1;
    lneighbor[0][2]=-1;
    neighbor[xsize+1][0]=-1;
    neighbor[ysize+1][1]=-1;
    neighbor[zsize+1][2]=-1;

#ifndef D3Q18
  l=0;
  for(z=0;z<(zsize+2);z++){                            /* initialisation of next step's index of n */
    for(y=0;y<(ysize+2);y++){
      for(x=0;x<(xsize+2);x++){
	for(i=0;i<n_veloc;i++){
	  new_x=(x+c_g[i][0]+xsize+2)%(xsize+2);  /* assume periodic boundaries for lattice + halo */ 
	  new_y=(y+c_g[i][1]+ysize+2)%(ysize+2);  /* for simplicity, halo region is destroyed by */
	  new_z=(z+c_g[i][2]+zsize+2)%(zsize+2);  /* propagation anyway, so exploit this */ 
	  next[l++]=i+(new_z*(xsize+2)*(ysize+2)+new_y*(xsize+2)+new_x)*n_veloc;
	}
      }
    }
  }
#endif

  ranseed=(myNum+1)*(compar.ranseed);   /* initialize ran3 */
  if (ranseed>0) {
//    if (!myNum) fprintf(stderr, "WARNING: ranseed is positive -> changing sign\n");
    ranseed=-ranseed;
    }
  ran3(&ranseed,dummy,1);   /* initialize ran3 */
  
  ran3(&ranseed,dummy,1);   /* test for overflow */
//  printf("and the first random is : %f\n",dummy[0]);
  if ((dummy[0]>1.0)||(dummy[0]<-1.0)) {
    fprintf(stderr, "ERROR: ranseed is too large -> ran3 does not work\n");
    ExitProgram(TERM_error);
  }      

  /*  if (boundary) InitBoundary(next,c_g,c_abs_sq_g);     initialize additional boundary */

    friction = compar.friction;            /* equilibration with total friction */
    noise = sqrt(6.0*friction*temperature/time_step);
}

/************************************************************************
end of ***********************************************************************/


void thermo_init_lb () {

    InitCommand();     /* set default values */ 

    new(); 
    prerun(); 

} 


/******************************************************************************/
void LB_Run () {

    calcfields(n,FALSE);
    calc_lbforce();
} 

void LB_propagate () {

      fluidstep+=time_step;
      if(fluidstep>=(tau-BOTT)) {           /* update fluid */
	// write_fluid_velocity(savefile,current_time-fluidstep);
	fluidstep=0.;
	calc_collis(n_abs_veloc, 
		    n_abs_which, coef_eq, c_abs_sq_g, c_sound_sq, 
		    lambda,fluct);
	halo_update();
	propagate_n();   /* shift distribution  */
        halo_update();
#ifndef D3Q18
	help=n;
	n=n_new;
	n_new=help;
#endif
	
      }

} 

/******************************************************************************/
/******************************************************************************/

void calc_momentum()
{ 
  Cell *cell;
  Particle *p;
  int i, c, k; 
  double momentum[3],result[3];
  T_IVECTOR size;    
  
  size[0]=xsize; size[1]=ysize; size[2]=zsize;
  momentum[0]=momentum[1]=momentum[2]= 0.0;
  
    
  for (c = 0; c < local_cells.n; c++) {
    cell = local_cells.cell[c];
    p  = cell->part;
    nMono = cell->n;

    for(i=0; i < nMono; i++) { 
      
      for (k= 0;k < 3;k++) {
         momentum[k] +=  p[i].m.v[k];
      }
    }


  }
//   printf("result %d %f\n",this_node,momentum[0]);

  MPI_Allreduce(momentum, result, 3, MPI_DOUBLE, MPI_SUM, MPI_COMM_WORLD);
   
//   printf("result %d %f\n",this_node,result[0]);

  for (c = 0; c < local_cells.n; c++) {
    cell = local_cells.cell[c];
    p  = cell->part;
    nMono = cell->n;

    for(i=0; i < nMono; i++) { 
      
       for (k= 0;k < 3;k++) {
         p[i].m.v[k] -= result[k];
       }
    }

  }
  fixmom = TRUE;
}

/*****************************************************************/
void add_lb_forces(Particle *p1, int ip)
{
      p1->f.f[0] +=force[ip][2];
      p1->f.f[1] +=force[ip][1];
      p1->f.f[2] +=force[ip][0];
}

/*******************************************************************************/
void calc_lbforce()
{ 
  Cell *cell;
  Particle *p;
  int i, c, k; 
  double  inva = 1.0/agrid,xpp;
  T_IVECTOR size;    
  if(temperature > 0.0 && !fixmom) calc_momentum();
  
  calcfields(n,FALSE);
  size[0]=xsize; size[1]=ysize; size[2]=zsize;

  for (c = 0; c < local_cells.n; c++) {
    cell = local_cells.cell[c];
    p  = cell->part;
    nMono = cell->n;
    alloc_part_mem(nMono);

    for(i=0; i < nMono; i++) { 
      /*******preparing coordinates************************************/

      for (k= 0;k < 3;k++) {
	int jj = 2-k;
      
	v[i][k] = p[i].m.v[jj]/time_step;
	xpp = p[i].r.p[jj];
	xint[i][k] = (int) (xpp*inva);
	xrel[i][k] = xpp*inva - xint[i][k];

	xint[i][k] = xint[i][k]%size[k]+1;               /* map to local PE */
      }
      xint[i][3]=1; 

    }

    calc_fluid_chain_interaction(0);

    for(i=0; i< nMono; i++) { 
      add_lb_forces(&p[i],i);
    }        
    
  }

/****************************************************************
       ghost cells
*****************************************************************/

if(n_nodes>1){
  for (c = 0; c < ghost_cells.n; c++) {
    cell = ghost_cells.cell[c];
    p  = cell->part;
    nMono = cell->n;
    alloc_part_mem(nMono);

    for(i=0; i < nMono; i++) { 

    xint[i][3]=1;

    for (k= 0;k < 3;k++) {
      int jj = 2-k;
      
      v[i][k] = p[i].m.v[jj]/time_step;
      xpp = p[i].r.p[jj]-my_left[jj]+agrid;

      xint[i][k] = (int) (xpp*inva);
      xrel[i][k] = xpp*inva - xint[i][k];

	if ( xint[i][k] < 0 || xint[i][k] > size[k])    
//	if ( xp[i][k] < my_left[jj] - agrid || xint[i][k] > my_right[jj])    
	  xint[i][3] = 0;  

      if( xint[i][k] == 0  && node_grid[jj] == 1) {
        xint[i][3] = 0;
	}

    }
    
}
  calc_fluid_chain_interaction(1);

}
}

}


void calc_fluid_chain_interaction(int iamghost){ 

  /**
  ** calculation of hydrodynamic interaction. fluid and monomer velocities
  ** are coupled by a friction coefficient
  **
  ** input: friction, position of monomers (xp), hydrodynamic fields (rho,j,n)
  **        stochastic force given to monomers (forces)
  ** output: hydrodynamic force is added to variable 'force' 
  **         and via momentum conservation: shifting of population n
  ** 
  ** this works only in three dimensions
  ** Author: Ahlrichs, 28/01/98
  ** Last Change: Lobaskin, 20/10/04
  */

  double inva=1.0/agrid;
  double invh=1.0/tau;
  double invMassRatio=1.0/MASSRATIO;
  double help_x,help_y,help_z;
  int x_int,y_int,z_int,ip,jp,i,k,l,m,index,index18,help_index_x,help_index_y,help_index_z,off4; 
  T_DVECTOR local_j,save_j,grid_j;
  double save_n[max_n_veloc*8];
  double help_rho;

  int yperiod = xsize+2;
  int zperiod = (ysize+2)*(xsize+2);

  double* nlocal;

  T_BOOL badrandoms;
  static int failcounter = 0;
  int localfails;
    
  for (ip=0;ip<nMono+MAX_RANREPEATE;ip++) {
    for (jp=0;jp<MAX_RANREPEATE;jp++) {
      store_rans[ip][jp][0] = noise*mibmran();
      store_rans[ip][jp][1] = noise*mibmran();
      store_rans[ip][jp][2] = noise*mibmran();
    }
  }
  store_rans[nMono+MAX_RANREPEATE-1][MAX_RANREPEATE-1][0] = -1.0-noise; /* for checking if store of random number is used up */

  for(jp=0;jp<nMono;jp++){

    if (xint[jp][3]){

    /* where in the lattice world is my monomer */

    x_int=(xint[jp][0]);
    y_int=(xint[jp][1]);
    z_int=(xint[jp][2]);

    help_x=ONE-xrel[jp][0];
    help_y=ONE-xrel[jp][1];
    help_z=ONE-xrel[jp][2];

    /* calculate fluid current at that point by linear interpolation */

    index = z_int*zperiod+y_int*yperiod+x_int; 

    help_index_x=neighbor[x_int][0]-x_int; /* incr. for x-neighbor */
    help_index_y=(neighbor[y_int][1]-y_int)*yperiod;
    help_index_z=(neighbor[z_int][2]-z_int)*zperiod;
   
    save_j[0]=ZERO;    /* interpolated value of u (!) at monomer pos. */
    save_j[1]=ZERO;
    save_j[2]=ZERO;

    off4 = 0;
    for(k=0;k<2;k++){
      for(l=0;l<2;l++){
	for(m=0;m<2;m++){
	  help_rho = ONE/rho[index];
	  save_j[0] += help_x*help_y*help_z*j[index][0]*help_rho;
	  save_j[1] += help_x*help_y*help_z*j[index][1]*help_rho;
	  save_j[2] += help_x*help_y*help_z*j[index][2]*help_rho;
          
	  index18 = index*n_veloc;
	  memcpy(&(save_n[off4]),&(n[index18]),n_veloc*sizeof(double));
	  off4+=18;
	  
	  index+=help_index_x;
	  help_x=ONE-help_x;
	  help_index_x=-help_index_x;
	}
	index+=help_index_y;
	help_y=ONE-help_y;
	help_index_y=-help_index_y;
      }
      index+=help_index_z;
      help_z=ONE-help_z;
      help_index_z=-help_index_z;
    }
    index18 = index*n_veloc;
    help_index_x*=n_veloc;
    help_index_y*=n_veloc;
    help_index_z*=n_veloc;

    localfails = 0;
    do {        /* try random numbers */

      badrandoms = FALSE;        /* flag is set if n is getting negative */

    for(i=0;i<3;i++) {   /* friction forces from velocity differ. */
      
      local_j[i] = -friction*(v[jp][i]-agrid*invh*save_j[i]);  
#ifdef DB
      printf("i %d  v[i] %12.6e %12.6e %12.6e save_j[i] %12.6e  local %12.6e rans %f\n",jp,v[jp][i],agrid,invh,save_j[i],local_j[i],store_rans[ip][0][i]);
#endif

      local_j[i] += store_rans[jp][0][i];  /* stochastic force on monomer */
    }

    if (!iamghost && transfer_momentum)  /* only if not ghost, add force to particle */
      for(i=0;i<3;i++) force[jp][i] = local_j[i]; 

    /* add contribution to monomer force */
 
    /* now remember momentum conservation: exchange population accordingly */

    /* tranfsorm units back, dt originates from time step */


    local_j[0]*=-inva*tau*invMassRatio*time_step;
    local_j[1]*=-inva*tau*invMassRatio*time_step;
    local_j[2]*=-inva*tau*invMassRatio*time_step;

    off4 = 0;
    for(k=0;k<2;k++){
      for(l=0;l<2;l++){
	for(m=0;m<2;m++){
         
	  grid_j[0]= help_x*help_y*help_z*local_j[0];
	  grid_j[1]= help_x*help_y*help_z*local_j[1];
	  grid_j[2]= help_x*help_y*help_z*local_j[2];

#ifdef D3Q18

    if(transfer_momentum){     
	  n[index18++] = save_n[off4++] + grid_j[0]*0.16666666666666666;
	  n[index18++] = save_n[off4++] - grid_j[0]*0.16666666666666666;
	  n[index18++] = save_n[off4++] + grid_j[1]*0.16666666666666666;
	  n[index18++] = save_n[off4++] - grid_j[1]*0.16666666666666666;
	  n[index18++] = save_n[off4++] + grid_j[2]*0.16666666666666666;
	  n[index18++] = save_n[off4++] - grid_j[2]*0.16666666666666666;
	  n[index18++] = save_n[off4++] + (grid_j[0]+grid_j[1])*0.08333333333333333;
	  n[index18++] = save_n[off4++] - (grid_j[0]+grid_j[1])*0.08333333333333333;
	  n[index18++] = save_n[off4++] + (grid_j[0]-grid_j[1])*0.08333333333333333;
	  n[index18++] = save_n[off4++] - (grid_j[0]-grid_j[1])*0.08333333333333333;
	  n[index18++] = save_n[off4++] + (grid_j[0]+grid_j[2])*0.08333333333333333;
	  n[index18++] = save_n[off4++] - (grid_j[0]+grid_j[2])*0.08333333333333333;
	  n[index18++] = save_n[off4++] + (grid_j[0]-grid_j[2])*0.08333333333333333;
	  n[index18++] = save_n[off4++] - (grid_j[0]-grid_j[2])*0.08333333333333333;
	  n[index18++] = save_n[off4++] + (grid_j[1]+grid_j[2])*0.08333333333333333;
	  n[index18++] = save_n[off4++] - (grid_j[1]+grid_j[2])*0.08333333333333333;
	  n[index18++] = save_n[off4++] + (grid_j[1]-grid_j[2])*0.08333333333333333;
	  n[index18++] = save_n[off4++] - (grid_j[1]-grid_j[2])*0.08333333333333333;
     }
	  index18-=n_veloc;
	  nlocal = &(n[index18]);

	  for (i=0;i<6;i++){
	    if (badrandoms) break;
	    if (*nlocal++<0.0) { 
	      if (failcounter%999==0) {fprintf(stdout,
	      "Warning: %d n negative (fluid_chain_interaction=> new randoms (time: %f, failcounter: %d) noise %f\n",
	      this_node, current_time,failcounter,noise); 
	      }
	      badrandoms = TRUE; localfails++;
	      store_rans[ip][0][0] = store_rans[ip][localfails][0]; 
	      store_rans[ip][0][1] = store_rans[ip][localfails][1]; 
	      store_rans[ip][0][2] = store_rans[ip][localfails][2];
	      failcounter++; 

	      if (store_rans[ip][0][0]<-noise) {
		fprintf(stderr, "%d ERROR in calc_fluid_chain_interaction: too many ranfails => INCREASE MAX_RANREPEATE\n",this_node);
		ExitProgram(TERM_error);
	      } 
	      break;
	    }	 
	    if (*nlocal++<0.0) {  
              if (failcounter%999==0) {fprintf(stdout,"Warning: n negative (fluid_chain_interaction=> new randoms (time: %f, failcounter: %d)\n",current_time,failcounter) ;  
              } 
              badrandoms = TRUE; localfails++;
	      store_rans[ip][0][0] = store_rans[ip][localfails][0]; 
	      store_rans[ip][0][1] = store_rans[ip][localfails][1]; 
	      store_rans[ip][0][2] = store_rans[ip][localfails][2];
              failcounter++; 
              if (store_rans[ip][0][0]<-noise) { 
                fprintf(stderr, "ERROR in calc_fluid_chain_interaction: too many ranfails=> INCREASE MAX_RANREPEATE\n"); 
                ExitProgram(TERM_error); 
              }
	      break;
	    }
	    if (*nlocal++<0.0) {   
              if (failcounter%999==0) {fprintf(stdout,"Warning: n negative (fluid_chain_interaction=> new randoms (time: %f, failcounter: %d)\n",current_time\
,failcounter) ;   
              }  
              badrandoms = TRUE;  localfails++;
	      store_rans[ip][0][0] = store_rans[ip][localfails][0]; 
	      store_rans[ip][0][1] = store_rans[ip][localfails][1]; 
	      store_rans[ip][0][2] = store_rans[ip][localfails][2];
              failcounter++;  
              if (store_rans[ip][0][0]<-noise) {  
                fprintf(stderr, "ERROR in calc_fluid_chain_interaction: too many ranfails=> INCREASE MAX_RANREPEATE\n");  
                ExitProgram(TERM_error);  
              }
	      break;
            } 

	  }

#else

	  off3 = 0;

	  for (i=0;i<2;i++){
	    double B_eq;
	    int n_which;
	    if (i==0) { n_which = 6; B_eq = 0.16666666666666666; }
	    else { n_which = 12; B_eq =0.08333333333333333; } 
	    
	    for(h=0;h<n_which;h++) {
	      n[index18] = save_n[off4] + B_eq*(grid_j[0]*c_g_d[off3][0]

			  +grid_j[1]*c_g_d[off3][1]+grid_j[2]*c_g_d[off3][2]);
	      if (n[index18]<0.0) { 
		if (failcounter%100==0) fprintf(stdout,"Warning: n negative (fluid_chain_interaction=> new randoms (time: %f, failcounter: %d)\n",current_time,failcounter) ; 
		badrandoms = TRUE;
		store_rans[ip][0] = store_rans[rancounter][0]; 
		store_rans[ip][1] = store_rans[rancounter][1]; 
		store_rans[ip][2] = store_rans[rancounter][2];
		failcounter++; rancounter++;
	      }
	      off3++;
	      off4++;
	      index18++;
	    }
	  }
	  index18-=n_veloc;
#endif

	  index18+=help_index_x;
	  help_x=ONE-help_x;
	  help_index_x=-help_index_x;
	}
	index18+=help_index_y;
	help_y=ONE-help_y;
	help_index_y=-help_index_y;
      }
      index18+=help_index_z;
      help_z=ONE-help_z;
      help_index_z=-help_index_z;
    }      

    } while (badrandoms);

    }  /* end of if(MonoLivesHere) */

  }

#ifdef DEBUG2  
     for(k=0;k<numPes;k++){
     if (k==myNum){
     for(i=0;i<nMono;i++){ printf("%d,%d: time %f force[%d]=%7f,%7f,%7f\n",myGroup,myGrpNum,sim_time,i,force[i][0],force[i][1],force[i][2]);
     }
     }
     MPI_Barrier(MPI_COMM_WORLD);
     } 
#endif

}

/******************************************************************************/

void halo_init(){
/* configuration of the halo regions */

  planeinfo[1].offset = (xsize+2)*(ysize+2)*n_veloc;    /* z=0 plane */
  planeinfo[1].doffset= (xsize+2)*(ysize+2)*(zsize+1)*n_veloc;
  planeinfo[1].stride = (xsize+2)*(ysize+2)*n_veloc;
  planeinfo[1].skip   = (xsize+2)*(ysize+2)*n_veloc;
  planeinfo[1].nblocks= 1;
  
  planeinfo[0].offset = (xsize+2)*(ysize+2)*zsize*n_veloc; /* z=zsize plane */
  planeinfo[0].doffset= 0*n_veloc;
  planeinfo[0].stride = (xsize+2)*(ysize+2)*n_veloc;
  planeinfo[0].skip   = (xsize+2)*(ysize+2)*n_veloc;
  planeinfo[0].nblocks= 1;

  MPI_Type_contiguous(planeinfo[0].stride*sizeof(double),MPI_BYTE,&xyPlane);
  MPI_Type_commit(&xyPlane);
 
  planeinfo[3].offset = (xsize+2)*n_veloc;          /* y=0 plane */
  planeinfo[3].doffset= (xsize+2)*(ysize+1)*n_veloc;
  planeinfo[3].stride = (xsize+2)*n_veloc;
  planeinfo[3].skip   = (xsize+2)*(ysize+2)*n_veloc;
  planeinfo[3].nblocks= zsize+2;

  planeinfo[2].offset = (xsize+2)*ysize*n_veloc;    /* y=gridpoints plane */
  planeinfo[2].doffset= 0*n_veloc;
  planeinfo[2].stride = (xsize+2)*n_veloc;
  planeinfo[2].skip   = (xsize+2)*(ysize+2)*n_veloc;
  planeinfo[2].nblocks= zsize+2;

  MPI_Type_hvector(planeinfo[2].nblocks,planeinfo[2].stride*sizeof(double),planeinfo[2].skip*sizeof(double),MPI_BYTE,&xzPlane);
  MPI_Type_commit(&xzPlane);

  planeinfo[5].offset = 1*n_veloc;                  /* x=0 plane */
  planeinfo[5].doffset= (xsize+1)*n_veloc;
  planeinfo[5].stride = 1*n_veloc;
  planeinfo[5].skip   = (xsize+2)*n_veloc;
  planeinfo[5].nblocks= (ysize+2)*(zsize+2);

  planeinfo[4].offset = xsize*n_veloc;        /* x=gridpoints plane */
  planeinfo[4].doffset= 0*n_veloc;
  planeinfo[4].stride = 1*n_veloc;
  planeinfo[4].skip   = (xsize+2)*n_veloc;
  planeinfo[4].nblocks= (ysize+2)*(zsize+2);

  MPI_Type_hvector(planeinfo[4].nblocks,planeinfo[4].stride*sizeof(double),planeinfo[4].skip*sizeof(double),MPI_BYTE,&yzPlane);
  MPI_Type_commit(&yzPlane);

}

/***************************************************************************************/
static void CopyPlane(int whichplane){

/* exchange by lb halo regions */

  int i;
  int offset = (planeinfo[whichplane].offset);   /* source offset */
  int doffset= (planeinfo[whichplane].doffset);  /* destination offset */
  int skip   = (planeinfo[whichplane].skip);
  int stride = (planeinfo[whichplane].stride)*sizeof(double);

  if (nPesPerDir[2-whichplane/2] == 1 ){ /*copy locally in that case */
    for (i=0;i<planeinfo[whichplane].nblocks;i++){
      memcpy(&(n[doffset]),&(n[offset]),stride);
      offset+=skip;
      doffset+=skip;
    }
  }

  else {    
    MPI_Status stat[2];
    MPI_Request request[2];

    int s_dir = whichplane;
    int  r_dir;
    if(s_dir%2==0) r_dir = s_dir+1;
    else           r_dir = s_dir-1;

      switch(s_dir) {
      case 0 :
      case 1 :
	  MPI_Irecv (&n[doffset],1,xyPlane,node_neighbors[s_dir],REQ_LB_SPREAD,MPI_COMM_WORLD,&request[0]);
	  MPI_Isend(&n[offset],1,xyPlane,node_neighbors[r_dir],REQ_LB_SPREAD,MPI_COMM_WORLD,&request[1]);

	MPI_Waitall(2,request,stat);
	break;
      case 2 :
      case 3 :
	  MPI_Irecv (&n[doffset],1,xzPlane,node_neighbors[s_dir],REQ_LB_SPREAD,MPI_COMM_WORLD,&request[0]);
	  MPI_Isend(&n[offset],1,xzPlane,node_neighbors[r_dir],REQ_LB_SPREAD,MPI_COMM_WORLD,&request[1]);

  	MPI_Waitall(2,request,stat);
	break;
      case 4 :
      case 5 : 
	  MPI_Irecv (&n[doffset],1,yzPlane,node_neighbors[s_dir],REQ_LB_SPREAD,MPI_COMM_WORLD,&request[0]);
	  MPI_Isend(&n[offset],1,yzPlane,node_neighbors[r_dir],REQ_LB_SPREAD,MPI_COMM_WORLD,&request[1]);

	MPI_Waitall(2,request,stat);
	break;
      }

  }

}
/************************************************************************************************/

#if 0
static void PrintPlane(int whichplane){
  int i,j;
  int offset = (planeinfo[whichplane].offset);   /* source offset */
  int doffset= (planeinfo[whichplane].doffset);  /* destination offset */
  int skip   = (planeinfo[whichplane].skip);
  int stride = (planeinfo[whichplane].stride)*sizeof(double);

  for (i=0;i<planeinfo[whichplane].nblocks;i++){
    printf("block: %d\n",i);
    for (j=0;j<stride;j+=n_veloc) printf("%d %f  %f, ", this_node,n[offset+j],n[doffset+j]);
    printf("\n");				 
    offset+=skip;
    doffset+=skip;
  }
}
#endif

/************************************************************************************************/
void halo_update(){
/* exchange by lb halo regions */
  int i;

  for (i=0;i<6;i++){
    CopyPlane(i); 
  }

  MPI_Barrier(MPI_COMM_WORLD);
}

/************************************************************************************************/


static void MyExit (int exitNr) {

  freemem();
  exit(exitNr);
}


T_TERM_FLAG Terminate (void) {

   if (fopen(stopName,"r")) {
   terminateFlag = TERM_sysstop;
   }
  return terminateFlag;
}

void FatalError (int numPe, char* where, char* what) {

  fprintf(stderr,"*** FATAL ERROR (%d) in function %s\n*** %s\n",numPe,
	  where,what);
  ExitProgram(TERM_error);
}


void RecoverableError (int numPe, char* where, char* what) {

  fprintf(stderr,"*** ERROR (%u) in function %s\n*** %s\n",numPe,where,what);
}


void Warning (int numPe, char* where, char* what) {

  fprintf(stderr,"*** WARNING (%u) in function %s\n*** %s\n",numPe,where,what);
}


void ExitProgram (T_TERM_FLAG why) {

  freemem();


  switch (why) {

  case TERM_kill : 
    printf("TERM-SIGNAL caught\n");
    MyExit(1);

  case TERM_cpulimit :
    printf("CPU-TIME-LIMIT exceeded\n");
    MyExit(1);

  case TERM_sysstop :
    printf("SYSTEM-STOP-FILE detected\n");
    MyExit(1);

  case TERM_shutdown :
    printf("SYSTEM-SHUTDOWN imminent\n");
    MyExit(1);

  case TERM_error :
    printf("aborting program\n");
    MyExit(2);

  case TERM_ok :
    printf("FULL SUCCESS !!!\n");
    break;

  case TERM_continue :
    break;
  }
}
