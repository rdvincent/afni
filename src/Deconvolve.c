/*---------------------------------------------------------------------------*/
/*
  This file contains routines for performing deconvolution analysis.

  File:    Deconvolve.c
  Author:  B. Douglas Ward
  Date:    31 August 1998

  Mod:     Restructured matrix calculations to improve execution speed.
  Date:    16 December 1998

  Mod:     Report mean square error from full model.
  Date:    04 January 1999

  Mod:     Earlier termination if unable to invert X matrix.
           (Avoids redundant error messages.)
  Date:    06 January 1999

  Mod:     Modifications for matrix calculation of general linear tests.
  Date:    02 July 1999

  Mod:     Additional statistical output (partial R^2 statistics).
  Date:    07 September 1999

  Mod:     Allow reading of multiple input stimulus functions from a single
           file by selection of individual columns.
  Date:    09 November 1999

  Mod:     Added options for writing the fitted full model time series (-fitts)
           and the residual error time series (-errts) to 3d+time datasets.
  Date:    22 November 1999

  Mod:     Added -censor option to allow operator to eliminate individual
           time points from the analysis.
  Date:    21 June 2000

  Mod:     Added -censor option to allow operator to eliminate individual
           time points from the analysis.
  Date:    21 June 2000

  Mod:     Added screen display of p-values.
  Date:    22 June 2000

  Mod:     Added -glt_label option for labeling the GLT matrix.
  Date:    23 June 2000

  Mod:     Added -concat option for analysis of a concatenated 3d+time dataset.
  Date:    26 June 2000

  Mod:     Increased size of screen output buffer.
  Date:    27 July 2000

  Mod:     Additional output with -nodata option (norm.std.dev.'s for
           GLT linear constraints).
  Date:    11 August 2000

  Mod:     Added -stim_nptr option, to allow input stim functions to be sampled
           at a multiple of the 1/TR rate.
  Date:    02 January 2001

  Mod:     Changes to eliminate constraints on number of stimulus time series,
           number of regressors, number of GLT's, and number of GLT linear
           constraints.
  Date:    10 May 2001

  Mod:     Made fstat_t2p() a static function to avoid conflicts on CYGWIN.
  Date:    08 Jan 2002 - RWCox

  Mod:     Added static function student_t2p() for display of p-values
           corresponding to the t-statistics.
  Date:    28 January 2002

  Mod:     Modifications to glt_analysis and report_results for enhanced screen
           output:  Display of p-values for individual stim function regression
           coefficients.  Display of t-stats and p-values for individual linear
           constraints within a GLT.
  Date:    29 January 2002

  Mod:     Allow user to specify which input stimulus functions are part of
           the baseline model.
  Date:    02 May 2002

  Mod:     Increased size of screen output buffer (again).
  Date:    02 December 2002

  Mod:     global variable legendre_polort replaces x^m with P_m(x)
  Date     15 Jul 2004 - RWCox

*/

/*---------------------------------------------------------------------------*/
/*
  Include linear regression analysis software.
*/

#include "RegAna.c"


/*---------------------------------------------------------------------------*/
static int legendre_polort = 1 ;  /* 15 Jul 2004 */

double legendre( double x , int m )   /* Legendre polynomials over [-1,1] */
{
   if( m < 0 ) return 1.0l ;    /* bad input */

   switch( m ){
    case 0: return 1.0l ;
    case 1: return x ;
    case 2: return (3.0l*x*x-1.0l)/2.0l ;
    case 3: return (5.0l*x*x-3.0l)*x/2.0l ;
    case 4: return ((35.0l*x*x-30.0l)*x*x+3.0l)/8.0l ;
    case 5: return ((63.0l*x*x-70.0l)*x*x+15.0l)*x/8.0l ;
    case 6: return (((231.0l*x*x-315.0l)*x*x+105.0l)*x*x-5.0l)/16.0l ;
    case 7: return (((429.0l*x*x-693.0l)*x*x+315.0l)*x*x-35.0l)*x/16.0l ;
    case 8: return ((((6435.0l*x*x-12012.0l)*x*x+6930.0l)*x*x-1260.0l)*x*x+35.0l)/128.0l;
   }

   /* order out of range: return Chebyshev instead (it's easy) */

        if(  x >=  1.0l ) x = 0.0 ;
   else if ( x <= -1.0l ) x = 3.14159265358979323846l ;
   else                   x = acos(x) ;
   return cos(m*x) ;
}

/*---------------------------------------------------------------------------*/
/*
   Initialize independent variable X matrix
*/

int init_indep_var_matrix
(
  int p,                      /* number of parameters in the full model */
  int qp,                     /* number of poly. trend baseline parameters */
  int polort,                 /* degree of polynomial for baseline model */
  int nt,                     /* number of images in input 3d+time dataset */
  int N,                      /* total number of images used in the analysis */
  int * good_list,            /* list of usable time points */
  int * block_list,           /* list of block (run) starting points */
  int num_blocks,             /* number of blocks (runs) */
  int num_stimts,             /* number of stimulus time series */
  float ** stimulus,          /* stimulus time series arrays */
  int * stim_length,          /* length of stimulus time series arrays */
  int * min_lag,              /* minimum time delay for impulse response */
  int * max_lag,              /* maximum time delay for impulse response */
  int * nptr,                 /* number of stim fn. time points per TR */
  matrix * xgood              /* independent variable matrix */
)

{
  int m;                    /* X matrix column index */
  int n;                    /* X matrix row index */
  int is;                   /* input stimulus index */
  int ilag;                 /* time lag index */
  int ib;                   /* block (run) index */
  int nfirst, nlast;        /* time boundaries of a block (run) */
  int mfirst, mlast;        /* column boundaries of baseline parameters
			       for a block (run) */

  float * stim_array;       /* stimulus function time series */
  matrix x;                 /* X matrix */


  /*----- Initialize X matrix -----*/
  matrix_initialize (&x);
  matrix_create (nt, p, &x);


  /*----- Set up columns of X matrix corresponding to
          the baseline (null hypothesis) signal model -----*/
  for (ib = 0;  ib < num_blocks;  ib++)
    {
      nfirst = block_list[ib];
      if (ib+1 < num_blocks)
	nlast = block_list[ib+1];
      else
	nlast = nt;

      for (n = nfirst;  n < nlast;  n++)
	{
	  mfirst = ib * (polort+1);
	  mlast  = (ib+1) * (polort+1);

          if( !legendre_polort ){                /* the old way: powers */
	    for (m = mfirst;  m < mlast;  m++)
	      x.elts[n][m] = pow ((double)(n-nfirst), (double)(m-mfirst));

          } else {            /* 15 Jul 2004: the new way: Legendre - RWCox */

            double xx , aa=2.0l/(nlast-nfirst-1.0l) ; /* map nfirst..nlast-1 */
            for( m=mfirst ; m < mlast ; m++ ){        /* to interval [-1,1] */
              xx = aa*(n-nfirst) - 1.0l ;
              x.elts[n][m] = legendre( xx , m-mfirst ) ;
            }
          }
	}
    }


  /*----- Set up columns of X matrix corresponding to
          time delayed versions of the input stimulus -----*/
  m = qp;
  for (is = 0;  is < num_stimts;  is++)
    {
      if (stim_length[is] < nt*nptr[is])
	{
	  DC_error ("Input stimulus time series is too short");
	  return (0);
	}
      stim_array = stimulus[is];
      for (ilag = min_lag[is];  ilag <= max_lag[is];  ilag++)
	{
	  for (n = 0;  n < nt;  n++)
	    {
	      if (n*nptr[is] < ilag)
		x.elts[n][m] = 0.0;
	      else
		x.elts[n][m] = stim_array[n*nptr[is]-ilag];
	    }
	  m++;
	}
    }


  /*----- Keep only those rows of the X matrix which correspond to
          usable time points -----*/
  matrix_extract_rows (x, N, good_list, xgood);
  matrix_destroy (&x);


  return (1);

}


/*---------------------------------------------------------------------------*/
/*
  Initialization for the regression analysis.
*/

int init_regression_analysis
(
  int p,                      /* number of parameters in full model */
  int qp,                     /* number of poly. trend baseline parameters */
  int num_stimts,             /* number of stimulus time series */
  int * baseline,             /* flag for stim function in baseline model */
  int * min_lag,              /* minimum time delay for impulse response */
  int * max_lag,              /* maximum time delay for impulse response */
  matrix xdata,               /* independent variable matrix */
  matrix * x_full,            /* extracted X matrix    for full model */
  matrix * xtxinv_full,       /* matrix:  1/(X'X)      for full model */
  matrix * xtxinvxt_full,     /* matrix:  (1/(X'X))X'  for full model */
  matrix * x_base,            /* extracted X matrix    for baseline model */
  matrix * xtxinvxt_base,     /* matrix:  (1/(X'X))X'  for baseline model */
  matrix * x_rdcd,            /* extracted X matrices  for reduced models */
  matrix * xtxinvxt_rdcd      /* matrix:  (1/(X'X))X'  for reduced models */
)

{
  int * plist = NULL;         /* list of model parameters */
  int ip, it;                 /* parameter indices */
  int is, js;                 /* stimulus indices */
  int im, jm;                 /* lag index */
  int ok;                     /* flag for successful matrix calculation */
  matrix xtxinv_temp;         /* intermediate results */


  /*----- Initialize matrix -----*/
  matrix_initialize (&xtxinv_temp);


  /*----- Initialize matrices for the baseline model -----*/
  plist = (int *) malloc (sizeof(int) * p);   MTEST(plist);
  for (ip = 0;  ip < qp;  ip++)
    plist[ip] = ip;
  it = ip = qp;
  for (is = 0;  is < num_stimts;  is++)
    {
      for (im = min_lag[is];  im <= max_lag[is];  im++)
	{
	  if (baseline[is])
	    {
	      plist[ip] = it;
	      ip++;
	    }
	  it++;
	}
    }
  ok = calc_matrices (xdata, ip, plist, x_base, &xtxinv_temp, xtxinvxt_base);
  if (!ok)  { matrix_destroy (&xtxinv_temp);  return (0); };


  /*----- Initialize matrices for stimulus functions -----*/
  for (is = 0;  is < num_stimts;  is++)
    {
      for (ip = 0;  ip < qp;  ip++)
	{
	  plist[ip] = ip;
	}

      it = ip = qp;

      for (js = 0;  js < num_stimts;  js++)
	{
	  for (jm = min_lag[js];  jm <= max_lag[js];  jm++)
	    {
	      if (is != js)
		{
		  plist[ip] = it;
		  ip++;
		}
	      it++;
	    }
	}

      ok = calc_matrices (xdata, p-(max_lag[is]-min_lag[is]+1),
		     plist, &(x_rdcd[is]), &xtxinv_temp, &(xtxinvxt_rdcd[is]));
      if (!ok)  { matrix_destroy (&xtxinv_temp);  return (0); };
    }


  /*----- Initialize matrices for full model -----*/
  for (ip = 0;  ip < p;  ip++)
    plist[ip] = ip;
  ok = calc_matrices (xdata, p, plist, x_full, xtxinv_full, xtxinvxt_full);
  if (!ok)  { matrix_destroy (&xtxinv_temp);  return (0); };


  /*----- Destroy matrix -----*/
  matrix_destroy (&xtxinv_temp);

  if (plist != NULL) { free(plist);  plist = NULL; }

  return (1);
}


/*---------------------------------------------------------------------------*/
/*
  Initialization for the general linear test analysis.
*/

int init_glt_analysis
(
  matrix xtxinv,              /* matrix:  1/(X'X)  for full model */
  int glt_num,                /* number of general linear tests */
  matrix * glt_cmat,          /* general linear test matrices */
  matrix * glt_amat,          /* constant GLT matrices for later use */
  matrix * cxtxinvct          /* matrices: C(1/(X'X))C' for GLT */

)

{
  int iglt;                   /* index for general linear test */
  int ok;                     /* flag for successful matrix inversion */


  for (iglt = 0;  iglt < glt_num;  iglt++)
    {
      ok = calc_glt_matrix (xtxinv, glt_cmat[iglt], &(glt_amat[iglt]),
			    &(cxtxinvct[iglt]));
      if (! ok)  return (0);
    }

  return (1);
}


/*---------------------------------------------------------------------------*/
/*
   Calculate results for this voxel.
*/

void regression_analysis
(
  int N,                    /* number of usable data points */
  int p,                    /* number of parameters in full model */
  int q,                    /* number of parameters in baseline model */
  int num_stimts,           /* number of stimulus time series */
  int * min_lag,            /* minimum time delay for impulse response */
  int * max_lag,            /* maximum time delay for impulse response */
  matrix x_full,            /* extracted X matrix    for full model */
  matrix xtxinv_full,       /* matrix:  1/(X'X)      for full model */
  matrix xtxinvxt_full,     /* matrix:  (1/(X'X))X'  for full model */
  matrix x_base,            /* extracted X matrix    for baseline model */
  matrix xtxinvxt_base,     /* matrix:  (1/(X'X))X'  for baseline model */
  matrix * x_rdcd,          /* extracted X matrices  for reduced models */
  matrix * xtxinvxt_rdcd,   /* matrix:  (1/(X'X))X'  for reduced models */
  vector y,                 /* vector of measured data */
  float rms_min,            /* minimum variation in data to fit full model */
  float * mse,              /* mean square error from full model */
  vector * coef_full,       /* regression parameters */
  vector * scoef_full,      /* std. devs. for regression parameters */
  vector * tcoef_full,      /* t-statistics for regression parameters */
  float * fpart,            /* partial F-statistics for the stimuli */
  float * rpart,            /* partial R^2 stats. for the stimuli */
  float * ffull,            /* full model F-statistics */
  float * rfull,            /* full model R^2 stats. */
  int * novar,              /* flag for insufficient variation in data */
  float * fitts,            /* full model fitted time series */
  float * errts             /* full model residual error time series */
)

{
  int is;                   /* input stimulus index */
  float sse_base;           /* error sum of squares, baseline model */
  float sse_rdcd;           /* error sum of squares, reduced model */
  float sse_full;           /* error sum of squares, full model */
  vector coef_temp;         /* intermediate results */


  /*----- Initialization -----*/
  vector_initialize (&coef_temp);


  /*----- Calculate regression coefficients for baseline model -----*/
  calc_coef (xtxinvxt_base, y, &coef_temp);


  /*----- Calculate the error sum of squares for the baseline model -----*/
  sse_base = calc_sse (x_base, coef_temp, y);


  /*----- Stop here if variation about baseline is sufficiently low -----*/
  if (sqrt(sse_base/N) < rms_min)
    {
      int it;

      *novar = 1;
      vector_create (p, coef_full);
      vector_create (p, scoef_full);
      vector_create (p, tcoef_full);
      for (is = 0;  is < num_stimts;  is++)
	{
	  fpart[is] = 0.0;
	  rpart[is] = 0.0;
	}
      for (it = 0;  it < N;  it++)
	{
	  fitts[it] = 0.0;
	  errts[it] = 0.0;
	}
      *mse = 0.0;
      *rfull = 0.0;
      *ffull = 0.0;
      vector_destroy (&coef_temp);
      return;
    }
  else
    *novar = 0;


  /*----- Calculate regression coefficients for the full model  -----*/
  calc_coef (xtxinvxt_full, y, coef_full);


  /*----- Calculate the error sum of squares for the full model -----*/
  sse_full = calc_sse_fit (x_full, *coef_full, y, fitts, errts);
  *mse = sse_full / (N-p);


  /*----- Calculate t-statistics for the regression coefficients -----*/
  calc_tcoef (N, p, sse_full, xtxinv_full,
	      *coef_full, scoef_full, tcoef_full);


  /*----- Determine significance of the individual stimuli -----*/
  for (is = 0;  is < num_stimts;  is++)
    {

      /*----- Calculate regression coefficients for reduced model -----*/
      calc_coef (xtxinvxt_rdcd[is], y, &coef_temp);


      /*----- Calculate the error sum of squares for the reduced model -----*/
      sse_rdcd = calc_sse (x_rdcd[is], coef_temp, y);


      /*----- Calculate partial F-stat for significance of the stimulus -----*/
      fpart[is] = calc_freg (N, p, p-(max_lag[is]-min_lag[is]+1),
			     sse_full, sse_rdcd);


      /*----- Calculate partial R^2 for this stimulus -----*/
      rpart[is] = calc_rsqr (sse_full, sse_rdcd);

    }


  /*----- Calculate coefficient of multiple determination R^2 -----*/
  *rfull = calc_rsqr (sse_full, sse_base);


  /*----- Calculate the total regression F-statistic -----*/
  *ffull = calc_freg (N, p, q, sse_full, sse_base);


  /*----- Dispose of vector -----*/
  vector_destroy (&coef_temp);

}


/*---------------------------------------------------------------------------*/
/*
  Perform the general linear test analysis for this voxel.
*/

void glt_analysis
(
  int N,                      /* number of data points */
  int p,                      /* number of parameters in the full model */
  matrix x,                   /* X matrix for full model */
  vector y,                   /* vector of measured data */
  float ssef,                 /* error sum of squares from full model */
  vector coef,                /* regression parameters for full model */
  int novar,                  /* flag for insufficient variation in data */
  matrix * cxtxinvct,         /* matrices: C(1/(X'X))C' for GLT */
  int glt_num,                /* number of general linear tests */
  int * glt_rows,             /* number of linear constraints in glt */
  matrix * glt_cmat,          /* general linear test matrices */
  matrix * glt_amat,          /* constant matrices */
  vector * glt_coef,          /* linear combinations from GLT matrices */
  vector * glt_tcoef,         /* t-statistics for GLT linear combinations */
  float * fglt,               /* F-statistics for the general linear tests */
  float * rglt                /* R^2 statistics for the general linear tests */
)

{
  int iglt;                   /* index for general linear test */
  int q;                      /* number of parameters in the rdcd model */
  float sser;                 /* error sum of squares, reduced model */
  vector rcoef;               /* regression parameters for reduced model */
  vector scoef;               /* std. devs. for regression parameters  */


  /*----- Initialization -----*/
  vector_initialize (&rcoef);
  vector_initialize (&scoef);


  /*----- Loop over multiple general linear tests -----*/
  for (iglt = 0;  iglt < glt_num;  iglt++)
    {
      /*----- Test for insufficient variation in data -----*/
      if (novar)
	{
	  vector_create (glt_rows[iglt], &glt_coef[iglt]);
	  vector_create (glt_rows[iglt], &glt_tcoef[iglt]);
	  fglt[iglt] = 0.0;
	  rglt[iglt] = 0.0;
	}
      else
	{
	  /*----- Calculate the GLT linear combinations -----*/
	  calc_lcoef (glt_cmat[iglt], coef, &glt_coef[iglt]);
	
	  /*----- Calculate t-statistics for GLT linear combinations -----*/
	  calc_tcoef (N, p, ssef, cxtxinvct[iglt],
		      glt_coef[iglt], &scoef, &glt_tcoef[iglt]);

	  /*----- Calculate regression parameters for the reduced model -----*/
          /*-----   (that is, the model in the column space of X but )  -----*/
          /*-----   (orthogonal to the restricted column space of XC')  -----*/
	  calc_rcoef (glt_amat[iglt], coef, &rcoef);

	  /*----- Calculate error sum of squares for the reduced model -----*/
	  sser = calc_sse (x, rcoef, y);

	  /*----- Calculate the F-statistic for this GLT -----*/
	  q = p - glt_rows[iglt];
	  fglt[iglt] = calc_freg (N, p, q, ssef, sser);

	  /*----- Calculate the R^2 statistic for this GLT -----*/
	  rglt[iglt] = calc_rsqr (ssef, sser);

	}
    }


  /*----- Dispose of vectors -----*/
  vector_destroy (&rcoef);
  vector_destroy (&scoef);

}


/*---------------------------------------------------------------------------*/
/*
  Convert t-values and F-values to p-value.
  These routines were copied and modified from: mri_stats.c
*/


static double student_t2p( double tt , double dof )
{
   double bb , xx , pp ;

   tt = fabs(tt);

   if( dof < 1.0 ) return 1.0 ;

   if (tt >= 1000.0)  return 0.0;

   bb = lnbeta( 0.5*dof , 0.5 ) ;
   xx = dof/(dof + tt*tt) ;
   pp = incbeta( xx , 0.5*dof , 0.5 , bb ) ;
   return pp ;
}


static double fstat_t2p( double ff , double dofnum , double dofden )
{
   int which , status ;
   double p , q , f , dfn , dfd , bound ;

   if (ff >= 1000.0)  return 0.0;

   which  = 1 ;
   p      = 0.0 ;
   q      = 0.0 ;
   f      = ff ;
   dfn    = dofnum ;
   dfd    = dofden ;

   cdff( &which , &p , &q , &f , &dfn , &dfd , &status , &bound ) ;

   if( status == 0 ) return q ;
   else              return 1.0 ;
}

/*---------------------------------------------------------------------------*/

static char lbuf[65536];  /* character string containing statistical summary */
static char sbuf[256];


void report_results
(
  int N,                      /* number of usable data points */
  int qp,                     /* number of poly. trend baseline parameters */
  int q,                      /* number of baseline model parameters */
  int p,                      /* number of full model parameters */
  int polort,                 /* degree of polynomial for baseline model */
  int * block_list,           /* list of block (run) starting points */
  int num_blocks,             /* number of blocks (runs) */
  int num_stimts,             /* number of stimulus time series */
  char ** stim_label,         /* label for each stimulus */
  int * baseline,             /* flag for stim function in baseline model */
  int * min_lag,              /* minimum time delay for impulse response */
  int * max_lag,              /* maximum time delay for impulse response */
  vector coef,                /* regression parameters */
  vector tcoef,               /* t-statistics for regression parameters */
  float * fpart,              /* partial F-statistics for the stimuli */
  float * rpart,              /* partial R^2 stats. for the stimuli */
  float ffull,                /* full model F-statistic */
  float rfull,                /* full model R^2 stat. */
  float mse,                  /* mean square error from full model */
  int glt_num,                /* number of general linear tests */
  char ** glt_label,          /* label for general linear tests */
  int * glt_rows,             /* number of linear constraints in glt */
  vector *  glt_coef,         /* linear combinations from GLT matrices */
  vector *  glt_tcoef,        /* t-statistics for GLT linear combinations */
  float * fglt,               /* F-statistics for the general linear tests */
  float * rglt,               /* R^2 statistics for the general linear tests */
  char ** label               /* statistical summary for ouput display */
)

{
  const int MAXBUF = 65000;   /* maximum buffer string length */
  int m;                      /* coefficient index */
  int is;                     /* stimulus index */
  int ilag;                   /* time lag index */

  int iglt;                   /* general linear test index */
  int ilc;                    /* linear combination index */

  double pvalue;              /* p-value corresponding to F-value */
  int r;                      /* number of parameters in the reduced model */

  int ib;                   /* block (run) index */
  int mfirst, mlast;        /* column boundaries of baseline parameters
			       for a block (run) */


  lbuf[0] = '\0' ;   /* make this a 0 length string to start */

  /** for each reference, make a string into sbuf **/


  /*----- Statistical results for baseline fit -----*/
  if (num_blocks == 1)
    {
      sprintf (sbuf, "\nBaseline: \n");
      if (strlen(lbuf) < MAXBUF)  strcat(lbuf,sbuf);
      for (m=0;  m < qp;  m++)
	{
	  sprintf (sbuf, "t^%d   coef = %10.4f    ", m, coef.elts[m]);
	  if (strlen(lbuf) < MAXBUF)  strcat(lbuf,sbuf) ;
	  sprintf (sbuf, "t^%d   t-st = %10.4f    ", m, tcoef.elts[m]);
	  if (strlen(lbuf) < MAXBUF)  strcat(lbuf,sbuf) ;
	  pvalue = student_t2p ((double)tcoef.elts[m], (double)(N-p));
	  sprintf (sbuf, "p-value  = %12.4e \n", pvalue);
	  if (strlen(lbuf) < MAXBUF)  strcat (lbuf, sbuf);
	}
    }
  else
    {
      for (ib = 0;  ib < num_blocks;  ib++)
	{
	  sprintf (sbuf, "\nBaseline for Run #%d: \n", ib+1);
	  if (strlen(lbuf) < MAXBUF)  strcat(lbuf,sbuf);
	
	  mfirst = ib * (polort+1);
	  mlast  = (ib+1) * (polort+1);
	  for (m = mfirst;  m < mlast;  m++)
	    {
	      sprintf (sbuf, "t^%d   coef = %10.4f    ",
		       m - mfirst, coef.elts[m]);
	      if (strlen(lbuf) < MAXBUF)  strcat(lbuf,sbuf) ;
	      sprintf (sbuf, "t^%d   t-st = %10.4f    ",
		       m - mfirst, tcoef.elts[m]);
	      if (strlen(lbuf) < MAXBUF)  strcat(lbuf,sbuf) ;
	      pvalue = student_t2p ((double)tcoef.elts[m], (double)(N-p));
	      sprintf (sbuf, "p-value  = %12.4e \n", pvalue);
	      if (strlen(lbuf) < MAXBUF)  strcat (lbuf, sbuf);
	    }
	}
    }


  /*----- Statistical results for stimulus response -----*/
  m = qp;
  for (is = 0;  is < num_stimts;  is++)
    {
      if (baseline[is])
	sprintf (sbuf, "\nBaseline: %s \n", stim_label[is]);	
      else
	sprintf (sbuf, "\nStimulus: %s \n", stim_label[is]);
      if (strlen(lbuf) < MAXBUF)  strcat(lbuf,sbuf);
      for (ilag = min_lag[is];  ilag <= max_lag[is];  ilag++)
	{
	  sprintf (sbuf,"h[%2d] coef = %10.4f    ", ilag, coef.elts[m]);
	  if (strlen(lbuf) < MAXBUF)  strcat(lbuf,sbuf) ;
	  sprintf  (sbuf,"h[%2d] t-st = %10.4f    ", ilag, tcoef.elts[m]);
	  if (strlen(lbuf) < MAXBUF)  strcat (lbuf, sbuf);
	  pvalue = student_t2p ((double)tcoef.elts[m], (double)(N-p));
	  sprintf (sbuf, "p-value  = %12.4e \n", pvalue);
	  if (strlen(lbuf) < MAXBUF)  strcat (lbuf, sbuf);
	  m++;
	}

      sprintf (sbuf, "       R^2 = %10.4f    ", rpart[is]);
      if (strlen(lbuf) < MAXBUF)  strcat (lbuf, sbuf);
      r = p - (max_lag[is]-min_lag[is]+1);
      sprintf (sbuf, "F[%2d,%3d]  = %10.4f    ", p-r, N-p, fpart[is]);
      if (strlen(lbuf) < MAXBUF)  strcat (lbuf, sbuf);
      pvalue = fstat_t2p ((double)fpart[is], (double)(p-r), (double)(N-p));
      sprintf (sbuf, "p-value  = %12.4e \n", pvalue);
      if (strlen(lbuf) < MAXBUF)  strcat (lbuf, sbuf);
    }


  /*----- Statistical results for full model -----*/
  sprintf (sbuf, "\nFull Model: \n");
  if (strlen(lbuf) < MAXBUF)  strcat (lbuf, sbuf);

  sprintf (sbuf, "       MSE = %10.4f \n", mse);
  if (strlen(lbuf) < MAXBUF)  strcat (lbuf, sbuf);

  sprintf (sbuf, "       R^2 = %10.4f    ", rfull);
  if (strlen(lbuf) < MAXBUF)  strcat (lbuf, sbuf);

  sprintf (sbuf, "F[%2d,%3d]  = %10.4f    ", p-q, N-p, ffull);
  if (strlen(lbuf) < MAXBUF)  strcat (lbuf, sbuf);
  pvalue = fstat_t2p ((double)ffull, (double)(p-q), (double)(N-p));
  sprintf (sbuf, "p-value  = %12.4e  \n", pvalue);
  if (strlen(lbuf) < MAXBUF)  strcat (lbuf, sbuf);


  /*----- Statistical results for general linear test -----*/
  if (glt_num > 0)
    {
      for (iglt = 0;  iglt < glt_num;  iglt++)
	{
	  sprintf (sbuf, "\nGeneral Linear Test: %s \n", glt_label[iglt]);
	  if (strlen(lbuf) < MAXBUF)  strcat (lbuf,sbuf);
	  for (ilc = 0;  ilc < glt_rows[iglt];  ilc++)
	    {
	      sprintf (sbuf, "LC[%d] coef = %10.4f    ",
		       ilc, glt_coef[iglt].elts[ilc]);
	      if (strlen(lbuf) < MAXBUF)  strcat (lbuf,sbuf);
	      sprintf (sbuf, "LC[%d] t-st = %10.4f    ",
		       ilc, glt_tcoef[iglt].elts[ilc]);
	      if (strlen(lbuf) < MAXBUF)  strcat (lbuf,sbuf);
	      pvalue = student_t2p ((double)glt_tcoef[iglt].elts[ilc],
				    (double)(N-p));
	      sprintf (sbuf, "p-value  = %12.4e \n", pvalue);
	      if (strlen(lbuf) < MAXBUF)  strcat (lbuf, sbuf);
	    }

	  sprintf (sbuf, "       R^2 = %10.4f    ", rglt[iglt]);
	  if (strlen(lbuf) < MAXBUF)  strcat (lbuf,sbuf);

	  r = p - glt_rows[iglt];
	  sprintf (sbuf, "F[%2d,%3d]  = %10.4f    ", p-r, N-p, fglt[iglt]);
	  if (strlen(lbuf) < MAXBUF)  strcat (lbuf, sbuf);
	  pvalue = fstat_t2p ((double)fglt[iglt],
			      (double)(p-r), (double)(N-p));
	  sprintf (sbuf, "p-value  = %12.4e  \n", pvalue);
	  if (strlen(lbuf) < MAXBUF)  strcat (lbuf, sbuf);
	}
    }

  if (strlen(lbuf) >= MAXBUF)
    strcat (lbuf, "\n\nWarning:  Screen output buffer is full. \n");

  *label = lbuf ;  /* send address of lbuf back in what label points to */

}


/*---------------------------------------------------------------------------*/




