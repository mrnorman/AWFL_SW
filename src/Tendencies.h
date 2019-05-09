
#ifndef _TENDENCIES_H_
#define _TENDENCIES_H_

#include "const.h"
#include "Parallel.h"
#include "SArray.h"
#include "Array.h"
#include "Riemann.h"
#include "Domain.h"
#include "Exchange.h"
#include "WenoLimiter.h"
#include "AderDT.h"
#include "TransformMatrices.h"

class Tendencies {

  Array<real> stateLimits;
  Array<real> fluxLimits;
  Array<real> flux;
  Array<real> src;
  SArray<real,tord> gllWts;
  TransformMatrices<real> trans;
  Riemann riem;
  SArray<real,ord,tord> to_gll;
  WenoLimiter<real> weno;
  SArray<real,ord,ord,ord> wenoRecon;
  AderDT ader;
  SArray<real,tord,tord> aderDerivX;
  SArray<real,tord,tord> aderDerivY;

public :


  inline void initialize(Domain &dom) {
    fluxLimits .setup(numState,2,dom.ny+1,dom.nx+1);
    stateLimits.setup(numState,2,dom.ny+1,dom.nx+1);
    flux       .setup(numState  ,dom.ny+1,dom.nx+1);
    src        .setup(numState  ,dom.ny  ,dom.nx  );

    SArray<real,ord,ord,ord> to_gll_tmp;

    // Setup the matrix to transform a stencil of ord cell averages into tord GLL points
    if (doWeno) {
      trans.coefs_to_gll_lower( to_gll_tmp );
    } else {
      trans.sten_to_gll_lower( to_gll_tmp );
    }
    for (int j=0; j<ord; j++) {
      for (int i=0; i<tord; i++) {
        to_gll(j,i) = to_gll_tmp(tord-1,j,i);
      }
    }

    trans.weno_sten_to_coefs(wenoRecon);

    SArray<real,tord,tord> g2c, c2d, c2g;
    trans.gll_to_coefs  (g2c);
    trans.coefs_to_deriv(c2d);
    trans.coefs_to_gll  (c2g);
    aderDerivX = (c2g * c2d * g2c) / dom.dx;
    aderDerivY = (c2g * c2d * g2c) / dom.dy;

    trans.get_gll_weights(gllWts);

  }


  // Transform ord stencil cell averages into tord GLL point values
  inline _HOSTDEV void reconStencil(SArray<real,ord> &stencil, SArray<real,tord> &gll) {
    SArray<real,ord> coefs;
    if (doWeno) {
      weno.compute_weno_coefs(wenoRecon,stencil,coefs);
    } else {
      for (int ii=0; ii<ord; ii++) {
        coefs(ii) = stencil(ii);
      }
    }

    for (int ii=0; ii<tord; ii++) {
      gll(ii) = 0.;
      for (int s=0; s<ord; s++) {
        gll(ii) += to_gll(s,ii) * coefs(s);
      }
    }
  }


  inline void compSWTendSD_X(Array<real> &state, Domain &dom, Exchange &exch, Parallel &par, Array<real> &tend) {

    //Exchange halos in the x-direction
    exch.haloInit      ();
    exch.haloPackN_x   (dom, state, numState);
    exch.haloExchange_x(dom, par);
    exch.haloUnpackN_x (dom, state, numState);

    // Reconstruct to tord GLL points in the x-direction
    for (int j=0; j<dom.ny; j++) {
      for (int i=0; i<dom.nx; i++) {
        SArray<real,numState,tord> gllState;  // GLL state values
        SArray<real,numState,tord> gllFlux;   // GLL flux values

        // Compute tord GLL points of the state vector
        for (int l=0; l<numState; l++) {
          SArray<real,ord> stencil;
          SArray<real,tord> gllPts;
          for (int ii=0; ii<ord; ii++) { stencil(ii) = state(l,hs+j,i+ii); }
          reconStencil(stencil,gllPts);
          for (int ii=0; ii<tord; ii++) { gllState(l,ii) = gllPts(ii); }
        }

        // Compute fluxes and at the GLL points
        for (int ii=0; ii<tord; ii++) {
          real h = gllState(idH ,ii);
          real u = gllState(idHU,ii) / h;
          real v = gllState(idHV,ii) / h;

          gllFlux(idH ,ii) = h*u;
          gllFlux(idHU,ii) = h*u*u + GRAV*h*h/2;
          gllFlux(idHV,ii) = h*u*v;
        }

        // Store state and flux limits into a globally indexed array
        for (int l=0; l<numState; l++) {
          // Store the left cell edge state and flux estimates
          stateLimits(l,1,j,i  ) = gllState(l,0);
          fluxLimits (l,1,j,i  ) = gllFlux (l,0);

          // Store the Right cell edge state and flux estimates
          stateLimits(l,0,j,i+1) = gllState(l,tord-1);
          fluxLimits (l,0,j,i+1) = gllFlux (l,tord-1);
        }

      }
    }

    //Reconcile the edge fluxes via MPI exchange.
    exch.haloInit      ();
    exch.edgePackN_x   (dom, stateLimits, numState);
    exch.edgePackN_x   (dom, fluxLimits , numState);
    exch.edgeExchange_x(dom, par);
    exch.edgeUnpackN_x (dom, stateLimits, numState);
    exch.edgeUnpackN_x (dom, fluxLimits , numState);

    // Riemann solver
    for (int j=0; j<dom.ny; j++) {
      for (int i=0; i<dom.nx+1; i++) {
        SArray<real,numState> s1, s2, f1, f2, upw;
        for (int l=0; l<numState; l++) {
          s1(l) = stateLimits(l,0,j,i);
          s2(l) = stateLimits(l,1,j,i);
          f1(l) = fluxLimits (l,0,j,i);
          f2(l) = fluxLimits (l,1,j,i);
        }
        riem.riemannX(s1, s2, f1, f2, upw);
        for (int l=0; l<numState; l++) {
          flux(l,j,i) = upw(l);
          flux(l,j,i) = ( fluxLimits(l,0,j,i) + fluxLimits(l,1,j,i) ) / 2;
        }
      }
    }

    // Form the tendencies
    for (int l=0; l<numState; l++) {
      for (int j=0; j<dom.ny; j++) {
        for (int i=0; i<dom.nx; i++) {
          tend(l,j,i) = - ( flux(l,j,i+1) - flux(l,j,i) ) / dom.dx;
        }
      }
    }
  }


  inline void compSWTendSD_Y(Array<real> &state, Domain &dom, Exchange &exch, Parallel &par, Array<real> &tend) {
    //Exchange halos in the y-direction
    exch.haloInit      ();
    exch.haloPackN_y   (dom, state, numState);
    exch.haloExchange_y(dom, par);
    exch.haloUnpackN_y (dom, state, numState);

    // Reconstruct to tord GLL points in the x-direction
    for (int j=0; j<dom.ny; j++) {
      for (int i=0; i<dom.nx; i++) {
        SArray<real,numState,tord> gllState;
        SArray<real,numState,tord> gllFlux;

        // Compute GLL points from cell averages
        for (int l=0; l<numState; l++) {
          SArray<real,ord> stencil;
          SArray<real,tord> gllPts;
          for (int ii=0; ii<ord; ii++) { stencil(ii) = state(l,j+ii,hs+i); }
          reconStencil(stencil,gllPts);
          for (int ii=0; ii<tord; ii++) { gllState(l,ii) = gllPts(ii); }
        }

        // Compute fluxes and at the GLL points
        for (int ii=0; ii<tord; ii++) {
          real h = gllState(idH ,ii);
          real u = gllState(idHU,ii) / h;
          real v = gllState(idHV,ii) / h;

          gllFlux(idH ,ii) = h*v;
          gllFlux(idHU,ii) = h*v*u;
          gllFlux(idHV,ii) = h*v*v + GRAV*h*h/2;
        }

        // Store state and flux limits into a globally indexed array
        for (int l=0; l<numState; l++) {
          // Store the left cell edge state and flux estimates
          stateLimits(l,1,j  ,i) = gllState(l,0);
          fluxLimits (l,1,j  ,i) = gllFlux (l,0);

          // Store the Right cell edge state and flux estimates
          stateLimits(l,0,j+1,i) = gllState(l,tord-1);
          fluxLimits (l,0,j+1,i) = gllFlux (l,tord-1);
        }

      }
    }

    //Reconcile the edge fluxes via MPI exchange.
    exch.haloInit      ();
    exch.edgePackN_y   (dom, stateLimits, numState);
    exch.edgePackN_y   (dom, fluxLimits , numState);
    exch.edgeExchange_y(dom, par);
    exch.edgeUnpackN_y (dom, stateLimits, numState);
    exch.edgeUnpackN_y (dom, fluxLimits , numState);

    for (int j=0; j<dom.ny+1; j++) {
      for (int i=0; i<dom.nx; i++) {
        SArray<real,numState> s1, s2, f1, f2, upw;
        for (int l=0; l<numState; l++) {
          s1(l) = stateLimits(l,0,j,i);
          s2(l) = stateLimits(l,1,j,i);
          f1(l) = fluxLimits (l,0,j,i);
          f2(l) = fluxLimits (l,1,j,i);
        }
        riem.riemannY(s1, s2, f1, f2, upw);
        for (int l=0; l<numState; l++) {
          flux(l,j,i) = upw(l);
          flux(l,j,i) = ( fluxLimits(l,0,j,i) + fluxLimits(l,1,j,i) ) / 2;
        }
      }
    }

    // Form the tendencies
    for (int l=0; l<numState; l++) {
      for (int j=0; j<dom.ny; j++) {
        for (int i=0; i<dom.nx; i++) {
          tend(l,j,i) = - ( flux(l,j+1,i) - flux(l,j,i) ) / dom.dy;
        }
      }
    }
  }


  inline void compSWTendADER_X(Array<real> &state, Domain &dom, Exchange &exch, Parallel &par, Array<real> &tend) {
    //Exchange halos in the x-direction
    exch.haloInit      ();
    exch.haloPackN_x   (dom, state, numState);
    exch.haloExchange_x(dom, par);
    exch.haloUnpackN_x (dom, state, numState);

    // Reconstruct to tord GLL points in the x-direction
    for (int j=0; j<dom.ny; j++) {
      for (int i=0; i<dom.nx; i++) {
        SArray<real,numState,tord,tord> stateDTs;  // GLL state values
        SArray<real,numState,tord,tord> fluxDTs;   // GLL flux values

        // Compute tord GLL points of the state vector
        for (int l=0; l<numState; l++) {
          SArray<real,ord> stencil;
          SArray<real,tord> gllPts;
          for (int ii=0; ii<ord; ii++) { stencil(ii) = state(l,hs+j,i+ii); }
          reconStencil(stencil,gllPts);
          for (int ii=0; ii<tord; ii++) { stateDTs(l,0,ii) = gllPts(ii); }
        }

        // Compute DTs of the state and flux, and collapse down into a time average
        ader.diffTransformSW_X( stateDTs , fluxDTs , aderDerivX );
        ader.timeAvg( stateDTs , dom );
        ader.timeAvg( fluxDTs  , dom );

        // Store state and flux limits into a globally indexed array
        for (int l=0; l<numState; l++) {
          // Store the left cell edge state and flux estimates
          stateLimits(l,1,j,i  ) = stateDTs(l,0,0);
          fluxLimits (l,1,j,i  ) = fluxDTs (l,0,0);

          // Store the Right cell edge state and flux estimates
          stateLimits(l,0,j,i+1) = stateDTs(l,0,tord-1);
          fluxLimits (l,0,j,i+1) = fluxDTs (l,0,tord-1);
        }

      }
    }

    //Reconcile the edge fluxes via MPI exchange.
    exch.haloInit      ();
    exch.edgePackN_x   (dom, stateLimits, numState);
    exch.edgePackN_x   (dom, fluxLimits , numState);
    exch.edgeExchange_x(dom, par);
    exch.edgeUnpackN_x (dom, stateLimits, numState);
    exch.edgeUnpackN_x (dom, fluxLimits , numState);

    // Riemann solver
    for (int j=0; j<dom.ny; j++) {
      for (int i=0; i<dom.nx+1; i++) {
        SArray<real,numState> s1, s2, f1, f2, upw;
        for (int l=0; l<numState; l++) {
          s1(l) = stateLimits(l,0,j,i);
          s2(l) = stateLimits(l,1,j,i);
          f1(l) = fluxLimits (l,0,j,i);
          f2(l) = fluxLimits (l,1,j,i);
        }
        riem.riemannX(s1, s2, f1, f2, upw);
        for (int l=0; l<numState; l++) {
          flux(l,j,i) = upw(l);
        }
      }
    }

    // Form the tendencies
    for (int l=0; l<numState; l++) {
      for (int j=0; j<dom.ny; j++) {
        for (int i=0; i<dom.nx; i++) {
          tend(l,j,i) = - ( flux(l,j,i+1) - flux(l,j,i) ) / dom.dx;
        }
      }
    }
  }


  inline void compSWTendADER_Y(Array<real> &state, Domain &dom, Exchange &exch, Parallel &par, Array<real> &tend) {
    //Exchange halos in the y-direction
    exch.haloInit      ();
    exch.haloPackN_y   (dom, state, numState);
    exch.haloExchange_y(dom, par);
    exch.haloUnpackN_y (dom, state, numState);

    // Reconstruct to tord GLL points in the x-direction
    for (int j=0; j<dom.ny; j++) {
      for (int i=0; i<dom.nx; i++) {
        SArray<real,numState,tord,tord> stateDTs;  // GLL state values
        SArray<real,numState,tord,tord> fluxDTs;   // GLL flux values

        // Compute GLL points from cell averages
        for (int l=0; l<numState; l++) {
          SArray<real,ord> stencil;
          SArray<real,tord> gllPts;
          for (int ii=0; ii<ord; ii++) { stencil(ii) = state(l,j+ii,hs+i); }
          reconStencil(stencil,gllPts);
          for (int ii=0; ii<tord; ii++) { stateDTs(l,0,ii) = gllPts(ii); }
        }

        // Compute DTs of the state and flux, and collapse down into a time average
        ader.diffTransformSW_Y( stateDTs , fluxDTs , aderDerivY );
        ader.timeAvg( stateDTs , dom );
        ader.timeAvg( fluxDTs  , dom );

        // Store state and flux limits into a globally indexed array
        for (int l=0; l<numState; l++) {
          // Store the left cell edge state and flux estimates
          stateLimits(l,1,j  ,i) = stateDTs(l,0,0);
          fluxLimits (l,1,j  ,i) = fluxDTs (l,0,0);

          // Store the Right cell edge state and flux estimates
          stateLimits(l,0,j+1,i) = stateDTs(l,0,tord-1);
          fluxLimits (l,0,j+1,i) = fluxDTs (l,0,tord-1);
        }

      }
    }

    //Reconcile the edge fluxes via MPI exchange.
    exch.haloInit      ();
    exch.edgePackN_y   (dom, stateLimits, numState);
    exch.edgePackN_y   (dom, fluxLimits , numState);
    exch.edgeExchange_y(dom, par);
    exch.edgeUnpackN_y (dom, stateLimits, numState);
    exch.edgeUnpackN_y (dom, fluxLimits , numState);

    for (int j=0; j<dom.ny+1; j++) {
      for (int i=0; i<dom.nx; i++) {
        SArray<real,numState> s1, s2, f1, f2, upw;
        for (int l=0; l<numState; l++) {
          s1(l) = stateLimits(l,0,j,i);
          s2(l) = stateLimits(l,1,j,i);
          f1(l) = fluxLimits (l,0,j,i);
          f2(l) = fluxLimits (l,1,j,i);
        }
        riem.riemannY(s1, s2, f1, f2, upw);
        for (int l=0; l<numState; l++) {
          flux(l,j,i) = upw(l);
        }
      }
    }

    // Form the tendencies
    for (int l=0; l<numState; l++) {
      for (int j=0; j<dom.ny; j++) {
        for (int i=0; i<dom.nx; i++) {
          tend(l,j,i) = - ( flux(l,j+1,i) - flux(l,j,i) ) / dom.dy;
        }
      }
    }
  }


};

#endif
