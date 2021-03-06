
#include <Rmath.h>
#include "common.h"

/* posterior wishart probability for the BGe score. */
double wpost(SEXP x, SEXP imaginary, SEXP phi_coef) {

int i = 0, n = LENGTH(x);
double mu = 0, phi = 0, tau = 0, rho = 0;
double oldtau = 0, oldmu = 0, logk = 0, logscale = 0, mscore = 0;
double res = 0, *xx = REAL(x), *c = REAL(phi_coef);
int *iss = INTEGER(imaginary);

  /* compute the mean and the variance of the data. */
  for (i = 0; i < n; i++)
    mu += xx[i];
  mu /= n;

  for (i = 0; i < n; i++)
    phi += (xx[i] - mu) * (xx[i] - mu);
  phi = phi / (n - 1) * (*c) ;

  /* set tau and rho. */
  tau = rho = *iss;

  for (i = 0; i < n; i++) {

    logscale = log(phi) + log1p(1.0/tau);
    logk = lgammafn(0.5 * (1.0 + rho)) - lgammafn(rho * 0.5);
    logk -= 0.5 * (logscale + log(M_PI));
    mscore = logk - 0.5 * (rho + 1) * log1p( (xx[i] - mu) * (xx[i] - mu) / exp(logscale) );
    res += mscore;

    oldtau = tau;
    oldmu  = mu;

    tau++;
    rho++;
    mu = (oldtau * mu + xx[i]) / tau;
    phi += (xx[i] - mu) * xx[i] + (oldmu - mu) * oldtau * oldmu;

  }/*FOR*/

  return res;

}/*WPOST*/

void build_tau(double **data, double *tau, int *ncols, int *nrows,
    int *imaginary, double *phi) {

int i = 0, j = 0, res_ncols = *ncols + 1;
double temp = 0;
double *mean = NULL, *mat = NULL;

  /* allocate mean vector and covariance matrix. */
  mean = alloc1dreal(*ncols);
  mat = alloc1dreal((*ncols) * (*ncols));

  /* compute the mean values.  */
  for (i = 0; i < *ncols; i++) {

    for (j = 0 ; j < *nrows; j++)
      mean[i] += data[i][j];

    mean[i] /= (*nrows);

  }/*FOR*/

  /* compute the covariance matrix... */
  c_covmat(data, mean, ncols, nrows, mat);
  /* ... multiply it by the phi coefficient... */
  for (i = 0; i < *ncols; i++)
    for (j = 0; j < *ncols; j++)
      mat[CMC(i, j, *ncols)] *= (*phi);

  /* ... compute the pseudoinverse... */
  c_ginv(mat, ncols, mat);

  /* ... and store it in the bottom-right corner of the tau matrix. */
  for (i = 1; i < res_ncols; i++)
    for (j = 1; j < res_ncols; j++)
      tau[CMC(i, j, res_ncols)] = mat[CMC(i - 1, j - 1, *ncols)];

  /* fill the top-right and bottom-left corners. */
  for (i = 1; i < *ncols + 1; i++) {

    temp = 0;

    for (j = 0; j < *ncols; j++)
      temp += mean[j] * mat[CMC(j, i - 1, *ncols)];

    tau[CMC(i, 0, res_ncols)] = tau[CMC(0, i, res_ncols)] = -temp;

  }/*FOR*/

  /* fill the top-left corner. */
  for (i = 1; i < res_ncols; i++)
    tau[CMC(0, 0, res_ncols)] += - mean[i - 1] * tau[CMC(i, 0, res_ncols)];

  tau[CMC(0, 0, res_ncols)] += 1/((double) *imaginary);

  /* perform the final (pseudo)inversion. */
  c_ginv(tau, &res_ncols, tau);

}/*BUILD_TAU*/

double cwpost(SEXP x, SEXP z, SEXP imaginary, SEXP phi_coef) {

int i = 0, j = 0, k = 0;
int ncols = LENGTH(z), num = LENGTH(x), tau_ncols = LENGTH(z) + 1;
int *iss = INTEGER(imaginary), rho = *iss + ncols;
double logscale = 0, logk = 0, xprod = 0, var_x = 0, zi_mu = 0, phi = 0;
double *xx = REAL(x), *phic = REAL(phi_coef), *workspace = NULL;
double res = 0, **zz = NULL, *zi = NULL, *mu = NULL, *delta_mu = NULL;
double *tau = NULL, *invtau = NULL, *old_tau = NULL, *old_mu = NULL;

  /* allocate a workspace vector. */
  workspace = alloc1dreal(tau_ncols);

  /* allocate and initialize the parent configuration. */
  zi = alloc1dreal(ncols + 1);
  zi[0] = 1;

  /* estimate mu and var_x. */
  mu = alloc1dreal(tau_ncols);
  old_mu = alloc1dreal(tau_ncols);
  delta_mu = alloc1dreal(tau_ncols);

  for (i = 0; i < num; i++)
    mu[0] += xx[i];
  mu[0] /= num;

  for (i = 0; i < num; i++)
    var_x += (xx[i] - mu[0]) * (xx[i] - mu[0]);
  var_x /= num - 1;

  /* initialize phi. */
  phi = var_x * (*phic);

  /* allocate and initialize an array of pointers for the variables. */
  zz = (double **) alloc1dpointer(ncols);
  for (j = 0; j < ncols; j++)
    zz[j] = REAL(VECTOR_ELT(z, j));

  /* allocate and initialize tau. */
  tau = alloc1dreal(tau_ncols * tau_ncols);
  old_tau = alloc1dreal(tau_ncols * tau_ncols);
  invtau = alloc1dreal(tau_ncols * tau_ncols);
  build_tau(zz, tau, &ncols, &num, iss, phic);
  memcpy(old_tau, tau, tau_ncols * tau_ncols * sizeof(double));
  c_ginv(tau, &tau_ncols, invtau);

  /* for each sample... */
  for (i = 0; i < num; i++) {

    /* ... extract the values of the parents ... */
    for (j = 0; j < ncols; j++)
      zi[j + 1] = zz[j][i];

    /* ... compute the Mahalanobis distance of z[i] ... */
    xprod = c_quadratic(zi, &tau_ncols, invtau, zi, workspace);

    /* ... compute the scale factor ... */
    logscale = log(phi) + log1p(xprod);
    logk = lgammafn(0.5 * (1 + rho)) - lgammafn(0.5 * rho);
    logk -= 0.5 * (logscale + log(M_PI));

    /* and then the score for the variable. */
    for (j = 0, zi_mu = 0; j < tau_ncols; j++)
      zi_mu += zi[j] * mu[j];

    res += logk - 0.5 * (1 + rho) *
             log1p((xx[i] - zi_mu) * (xx[i] - zi_mu) / exp(logscale));

    /* For the next iteration, update the tau matrix ... */
    memcpy(old_tau, tau, tau_ncols * tau_ncols * sizeof(double));

    for (j = 0; j < tau_ncols; j++)
      for (k = j; k < tau_ncols; k++)
        tau[CMC(j, k, tau_ncols)] = tau[CMC(k, j, tau_ncols)] =
          tau[CMC(j, k, tau_ncols)] + zi[j] * zi[k];

    /* ... its inverse  ... */
    c_finv(tau, &tau_ncols, invtau);

    /* ... update the mu vector ... */
    memcpy(old_mu, mu, tau_ncols * sizeof(double));
    c_rotate(invtau, old_tau, mu, &(xx[i]), zi, &tau_ncols, workspace);

    /* ... update rho (ISS + sample size evaluated at the current iteration) ... */
    rho++;

    /* ... and update phi. */
    for (j = 0; j < tau_ncols; j++)
      delta_mu[j] = old_mu[j] - mu[j];
    for (j = 0, zi_mu = 0; j < tau_ncols; j++)
      zi_mu += zi[j] * mu[j];

    phi += (xx[i] - zi_mu) * xx[i] +
             c_quadratic(delta_mu, &tau_ncols, old_tau, old_mu, workspace);

  }/*FOR*/

  return res;

}/*CWPOST*/

SEXP wishart_node(SEXP target, SEXP x, SEXP data, SEXP iss, SEXP phi, SEXP prior,
    SEXP beta, SEXP debug) {

int *imaginary = INTEGER(iss), *debuglevel = LOGICAL(debug);
int n = LENGTH(VECTOR_ELT(data, 0));
char *phi_str = (char *)CHAR(STRING_ELT(phi, 0));
char *t = (char *)CHAR(STRING_ELT(target, 0));
double *pr = NULL, *pb = NULL;
SEXP phi_coef, nodes, node_t, parents, parent_vars, data_t, prob, prior_prob;

  PROTECT(phi_coef = allocVector(REALSXP, 1));

  /* compute the phi multiplier. */
  if (strcmp(phi_str, "bottcher") == 0)
    NUM(phi_coef) = (double)(n - 1) / (double)(n) * (double)(*imaginary - 1);
  else
    NUM(phi_coef) = (double)(n - 1) / (double)n * 
                 (double)(*imaginary) / (double) (*imaginary + 1) * (double)(*imaginary - 2);

  if (*debuglevel > 0) {

    Rprintf("----------------------------------------------------------------\n");
    Rprintf("* processing node %s.\n", t);

  }/*THEN*/

  /* get the node cached information. */
  nodes = getListElement(x, "nodes");
  node_t = getListElement(nodes, t);
  /* get the parents of the node. */
  parents = getListElement(node_t, "parents");
  /* extract the node's column from the data frame. */
  data_t = c_dataframe_column(data, target, TRUE, FALSE);
  /* compute the prior probability component for the node. */
  PROTECT(prior_prob = graph_prior_prob(prior, target, node_t, beta, debug));
  pr = REAL(prior_prob);
  /* allocate the return value. */
  PROTECT(prob = allocVector(REALSXP, 1));
  pb = REAL(prob);

  if (LENGTH(parents) == 0) {

    *pb = wpost(data_t, iss, phi_coef);

  }/*THEN*/
  else {

    PROTECT(parent_vars = c_dataframe_column(data, parents, FALSE, FALSE));
    /* compute the marginal likelihood. */
    *pb = cwpost(data_t, parent_vars, iss, phi_coef);

    UNPROTECT(1);

  }/*ELSE*/

  if (*debuglevel > 0) {

    Rprintf("  > prior probability is %lf.\n", *pb);
    Rprintf("  > posterior density is %lf.\n", *pr);

  }/*THEN*/

  /* add the (log)prior to the marginal (log)likelihood to get the (log)posterior. */
  *pb += *pr;

  UNPROTECT(3);

  return prob;

}/*WISHART_NODE*/
