
#pragma once

#include "const.h"
#include "TransformMatrices.h"
#include "WenoLimiter.h"


/*********************************************
 ***** Required inside the Spatial class *****
 *********************************************
class Location;
    - Stores the indices of a single location on the grid

class StateArr; // OR
typedef [class] StateArr;
    - Declare a type for the model state

class TendArr; // OR
typedef [class] TendArr;
    - Declare a type for the model tendencies
    - It must have room for the nTimeDerivs dimension for the time integrator

StateArr createStateArr()
    - Create and return a new StateArr object

TendArr createTendArr(int nTimeDerivs)
    - Create and return a new TendArr object

YAKL_INLINE real &get(StateArr const &state , Location const &loc , int splitIndex)
    - Return the state value at the given location

YAKL_INLINE real &get(TendArr const &tend , Location const &loc , int timeDeriv , int splitIndex)
    - Return the tendency value at the given location

int numSplit()
    - Return the number of split components for this operator
    - The temporal operator will iterate through the splittings

real computeTimeStep(real cfl)
    - Return the time step in seconds from the cfl value

void init(int nTimeDerivs, bool timeAvg, std::string inFile)
    - Initialize internal data structures

void initState( StateArr &state )
    - Initialize the state

void computeTendencies( StateArr const &state , TendArr &tend , real dt , int splitIndex)
    - Compute tendency and time derivatives of the tendency if they are requested

template <class F> void applyTendencies( F const &applySingleTendency , int splitIndex )
    - Loop through the domain, and apply tendencies to the state

const char * getSpatialName()
    - Return the name and info for this spatial operator

void output(StateArr const &state, real etime)
    - Output to file
*/


class Spatial {
public:

  static_assert(nTimeDerivs == 1 , "ERROR: This operator doesn't support nTimeDerivs > 1 yet");

  int static constexpr hs = (ord-1)/2;
  int static constexpr numState = 3;

  // Stores a single index location
  struct Location {
    int l;
    int j;
    int i;
  };

  typedef real3d StateArr;  // Spatial index

  typedef real4d TendArr;   // (time derivative , spatial index)

  // Flux time derivatives
  real5d fwaves;
  real4d surf_limits;
  real2d bath;
  // For non-WENO interpolation
  SArray<real,2,ord,ngll> sten_to_gll;
  SArray<real,2,ord,ngll> sten_to_deriv_gll;
  SArray<real,2,ord,ngll> coefs_to_gll;
  SArray<real,2,ord,ngll> coefs_to_deriv_gll;
  SArray<real,3,ord,ord,ord> wenoRecon;
  SArray<real,1,hs+2> idl;
  real sigma;
  // For ADER spatial derivative computation
  SArray<real,2,ngll,ngll> derivMatrix;
  // For quadrature
  SArray<real,1,ord> gllWts_ord;
  SArray<real,1,ord> gllPts_ord;
  SArray<real,1,ngll> gllWts_ngll;
  SArray<real,1,ngll> gllPts_ngll;

  int static constexpr idH = 0;  // rho
  int static constexpr idU = 1;  // u
  int static constexpr idV = 2;  // v

  int static constexpr DATA_SPEC_DAM                  = 1;
  int static constexpr DATA_SPEC_LAKE_AT_REST_PERT_1D = 2;
  int static constexpr DATA_SPEC_DAM_RECT_1D          = 3;
  int static constexpr DATA_SPEC_LAKE_AT_REST_PERT_2D = 4;

  int static constexpr BC_WALL     = 0;
  int static constexpr BC_PERIODIC = 1;
  int static constexpr BC_OPEN     = 2;

  bool sim1d;

  real grav;
  real dx;
  real dy;
  int  bc_x;
  int  bc_y;
  bool dimSwitch;

  real mass_init;

  // Values read from input file
  int         nx;
  int         ny;
  bool        doweno;
  std::string outFile;
  int         dataSpec;
  real        xlen;
  real        ylen;
  std::string bc_x_str;
  std::string bc_y_str;

  static_assert(ord%2 == 1,"ERROR: ord must be an odd integer");


  StateArr createStateArr() {
    return StateArr("stateArr",numState,ny+2*hs,nx+2*hs);
  }



  TendArr createTendArr() {
    return TendArr("tendArr",numState,nTimeDerivs,ny,nx);
  }



  YAKL_INLINE real &get(StateArr const &state , Location const &loc , int splitIndex) {
    return state(loc.l,hs+loc.j,hs+loc.i);
  }



  YAKL_INLINE real &get(TendArr const &tend , Location const &loc , int timeDeriv , int splitIndex) {
    return tend(loc.l,timeDeriv,loc.j,loc.i);
  }



  int numSplit() {
    return 2;
  }



  real computeTimeStep(real cfl, StateArr const &state) {
    auto &grav = this->grav;
    auto &dx   = this->dx;
    auto &dy   = this->dy;
    real2d dt2d("dt2d",ny,nx);
    parallel_for( Bounds<2>(ny,nx) , YAKL_LAMBDA (int j, int i) {
      real h = state(idH,hs+j,hs+i);
      real u = state(idU,hs+j,hs+i);
      real v = state(idV,hs+j,hs+i);
      real gw = sqrt(grav*h);
      real dtx = cfl*dx/max( abs(u+gw) , abs(u-gw) );
      real dty = cfl*dy/max( abs(v+gw) , abs(v-gw) );
      dt2d(j,i) = min(dtx,dty);
    });
    yakl::ParallelMin<real,memDevice> pmin(ny*nx);
    return pmin(dt2d.data());
  }



  // Initialize crap needed by recon()
  void init(std::string inFile) {
    dimSwitch = true;

    // Read the input file
    YAML::Node config = YAML::LoadFile(inFile);
    if ( !config             ) { endrun("ERROR: Invalid YAML input file"); }
    if ( !config["nx"]       ) { endrun("ERROR: No nx in input file"); }
    if ( !config["ny"]       ) { endrun("ERROR: No ny in input file"); }
    if ( !config["xlen"]     ) { endrun("ERROR: No xlen in input file"); }
    if ( !config["ylen"]     ) { endrun("ERROR: No ylen in input file"); }
    if ( !config["bc_x"]     ) { endrun("ERROR: No bc_x in input file"); }
    if ( !config["bc_y"]     ) { endrun("ERROR: No bc_y in input file"); }
    if ( !config["initData"] ) { endrun("ERROR: No dataSpec in input file"); }
    if ( !config["outFile"]  ) { endrun("ERROR: No outFile in input file"); }

    nx = config["nx"].as<int>();
    ny = config["ny"].as<int>();

    sim1d = ny == 1;

    xlen = config["xlen"].as<real>();
    ylen = config["ylen"].as<real>();

    std::string dataStr = config["initData"].as<std::string>();
    if        (dataStr == "dam") {
      dataSpec = DATA_SPEC_DAM;
      grav = 1;
    } else if (dataStr == "lake_at_rest_pert_1d") {
      assert( sim1d );
      dataSpec = DATA_SPEC_LAKE_AT_REST_PERT_1D;
      grav = 9.81;
    } else if (dataStr == "dam_rect_1d") {
      assert( sim1d );
      dataSpec = DATA_SPEC_DAM_RECT_1D;
      grav = 9.81;
    } else if (dataStr == "lake_at_rest_pert_2d") {
      dataSpec = DATA_SPEC_LAKE_AT_REST_PERT_2D;
      grav = 9.81;
    } else {
      endrun("ERROR: Invalid dataSpec");
    }

    std::string bc_x_str = config["bc_x"].as<std::string>();
    if        (bc_x_str == "periodic") {
      bc_x = BC_PERIODIC;
    } else if (bc_x_str == "wall") {
      bc_x = BC_WALL;
    } else if (bc_x_str == "open") {
      bc_x = BC_OPEN;
    } else {
      endrun("ERROR: Invalid bc_x");
    }

    std::string bc_y_str = config["bc_y"].as<std::string>();
    if        (bc_y_str == "periodic") {
      bc_y = BC_PERIODIC;
    } else if (bc_y_str == "wall") {
      bc_y = BC_WALL;
    } else if (bc_y_str == "open") {
      bc_y = BC_OPEN;
    } else {
      endrun("ERROR: Invalid bc_y");
    }

    outFile = config["outFile"].as<std::string>();

    dx = xlen/nx;
    dy = ylen/ny;

    // Store to_gll and wenoRecon
    TransformMatrices::weno_sten_to_coefs(this->wenoRecon);
    {
      SArray<real,2,ord, ord>    s2c;
      SArray<real,2,ord, ord>    c2d;
      SArray<real,2,ord,ngll>    c2g_lower;

      TransformMatrices::sten_to_coefs(s2c);
      TransformMatrices::coefs_to_gll_lower(c2g_lower);
      TransformMatrices::coefs_to_deriv(c2d);

      coefs_to_gll = c2g_lower;
      coefs_to_deriv_gll = c2g_lower * c2d;
      sten_to_gll = c2g_lower * s2c;
      sten_to_deriv_gll = c2g_lower * c2d * s2c;
    }
    // Store ader derivMatrix
    {
      SArray<real,2,ngll,ngll> g2c;
      SArray<real,2,ngll,ngll> c2d;
      SArray<real,2,ngll,ngll> c2g;

      TransformMatrices::gll_to_coefs  (g2c);
      TransformMatrices::coefs_to_deriv(c2d);
      TransformMatrices::coefs_to_gll  (c2g);

      this->derivMatrix = c2g * c2d * g2c;
    }
    TransformMatrices::get_gll_points (this->gllPts_ord);
    TransformMatrices::get_gll_weights(this->gllWts_ord);
    TransformMatrices::get_gll_points (this->gllPts_ngll);
    TransformMatrices::get_gll_weights(this->gllWts_ngll);

    #if (ORD != 1)
      weno::wenoSetIdealSigma(this->idl,this->sigma);
    #endif

    fwaves       = real5d("fwaves"     ,numState,nTimeDerivs,2,ny+1,nx+1);
    surf_limits  = real4d("surf_limits",         nTimeDerivs,2,ny+1,nx+1);
    bath         = real2d("bathymetry" ,ny+2*hs,nx+2*hs);
  }



  // Initialize the state
  void initState( StateArr &state ) {
    memset( state , 0._fp );
    memset( bath  , 0._fp );

    auto &bath       = this->bath      ;
    auto &nx         = this->nx        ;
    auto &ny         = this->ny        ;
    auto &dataSpec   = this->dataSpec  ;
    auto &bc_x       = this->bc_x      ;
    auto &bc_y       = this->bc_y      ;
    auto &gllPts_ord = this->gllPts_ord;
    auto &gllWts_ord = this->gllWts_ord;
    auto &dx         = this->dx        ;
    auto &dy         = this->dy        ;
    auto &xlen       = this->xlen      ;

    parallel_for( Bounds<2>(ny,nx) , YAKL_LAMBDA (int j, int i) {
      if        (dataSpec == DATA_SPEC_DAM) {
        if (i > nx/4 && i < 3*nx/4 && j > ny/4 && j < 3*ny/4) {
          state(idH,hs+j,hs+i) = 3;
          state(idU,hs+j,hs+i) = 0;
        } else {
          state(idH,hs+j,hs+i) = 1;
          state(idU,hs+j,hs+i) = 0;
        }
        bath(hs+j,hs+i) = 0;
      } else if (dataSpec == DATA_SPEC_LAKE_AT_REST_PERT_1D) {
        for (int ii=0; ii < ord; ii++) {
          real xloc = (i+0.5_fp)*dx + gllPts_ord(ii)*dx;
          real b  = 0;
          real surf = 1;
          if (xloc >= 1.4_fp && xloc <= 1.6_fp) {
            b = (1 + cos(10*M_PI*(xloc-0.5_fp))) / 4;
          }
          if (xloc >= 1.1_fp && xloc <= 1.2_fp) {
            surf = 1.001;
          }
          state(idH,hs+j,hs+i) += (surf - b) * gllWts_ord(ii);
          bath (    hs+j,hs+i) += b          * gllWts_ord(ii);
        }
      } else if (dataSpec == DATA_SPEC_DAM_RECT_1D) {
        for (int ii=0; ii < ord; ii++) {
          real xloc = (i+0.5_fp)*dx + gllPts_ord(ii)*dx;
          real b  = 0;
          real surf = 15;
          if (abs(xloc-xlen/2) <= xlen/8) {
            b = 8;
          }
          if (xloc <= 750) {
            surf = 20;
          }
          state(idH,hs+j,hs+i) += (surf - b) * gllWts_ord(ii);
          bath (    hs+j,hs+i) += b          * gllWts_ord(ii);
        }
      } else if (dataSpec == DATA_SPEC_LAKE_AT_REST_PERT_2D) {
        for (int jj=0; jj < ord; jj++) {
          for (int ii=0; ii < ord; ii++) {
            real xloc = (i+0.5_fp)*dx + gllPts_ord(ii)*dx;
            real yloc = (j+0.5_fp)*dy + gllPts_ord(jj)*dy;
            real b = 0.8_fp*exp( -5*(xloc-0.9_fp)*(xloc-0.9_fp) - 50*(yloc-0.5_fp)*(yloc-0.5_fp) );
            real surf = 1;
            if (xloc >= 0.05_fp && xloc <= 0.15_fp) {
              surf = 1.01;
            } else {
              surf = 1;
            }
            state(idH,hs+j,hs+i) += (surf - b) * gllWts_ord(ii) * gllWts_ord(jj);
            bath (    hs+j,hs+i) += b          * gllWts_ord(ii) * gllWts_ord(jj);
          }
        }
      }
    });
    // x-direction boundaries for bathymetry
    parallel_for( Bounds<2>(ny+2*hs,hs) , YAKL_LAMBDA (int j, int ii) {
      if        (bc_x == BC_WALL || bc_x == BC_OPEN) {
        bath(j,      ii) = bath(j,hs     );
        bath(j,nx+hs+ii) = bath(j,hs+nx-1);
      } else if (bc_x == BC_PERIODIC) {
        bath(j,      ii) = bath(j,nx+ii);
        bath(j,nx+hs+ii) = bath(j,hs+ii);
      }
    });
    // y-direction boundaries for bathymetry
    parallel_for( Bounds<2>(nx+2*hs,hs) , YAKL_LAMBDA (int i, int ii) {
      if        (bc_y == BC_WALL || bc_y == BC_OPEN) {
        bath(      ii,i) = bath(hs     ,i);
        bath(ny+hs+ii,i) = bath(hs+ny-1,i);
      } else if (bc_y == BC_PERIODIC) {
        bath(      ii,i) = bath(ny+ii,i);
        bath(ny+hs+ii,i) = bath(hs+ii,i);
      }
    });

    real2d mass("mass",ny,nx);
    parallel_for( Bounds<2>(ny,nx) , YAKL_LAMBDA (int j, int i) {
      real h = state(idH,hs+j,hs+i);
      mass(j,i) = h;
    });
    yakl::ParallelSum<real,memDevice> psum(ny*nx);
    mass_init = psum(mass.data());
  }

  

  YAKL_INLINE real cosine(real const x, real const x0, real const xrad, real const amp, real const pwr) {
    real val = 0;
    real xn = (x-x0)/xrad;
    real dist = sqrt( xn*xn );
    if (dist <= 1._fp) {
      val = amp * pow( (cos(M_PI*dist)+1)/2 , pwr );
    }
    return val;
  }



  // Compute state and tendency time derivatives from the state
  void computeTendencies( StateArr &state , TendArr &tend , real dt , int splitIndex ) {
    if (dimSwitch) {
      if      (splitIndex == 0) { computeTendenciesX( state , tend , dt ); }
      else if (splitIndex == 1) {
        if (sim1d) {
          memset( tend , 0._fp );
        } else {
          computeTendenciesY( state , tend , dt );
        }
      }
    } else {
      if      (splitIndex == 0) {
        if (sim1d) {
          memset( tend , 0._fp );
        } else {
          computeTendenciesY( state , tend , dt );
        }
      } else if (splitIndex == 1) { computeTendenciesX( state , tend , dt ); }
    }
    if (splitIndex == numSplit()-1) {
      dimSwitch = ! dimSwitch;
    }
  }



  // Compute state and tendency time derivatives from the state
  void computeTendenciesX( StateArr &state , TendArr &tend , real dt ) {
    auto &bc_x               = this->bc_x              ;
    auto &nx                 = this->nx                ;
    auto &dx                 = this->dx                ;
    auto &bath               = this->bath              ;
    auto &s2g                = this->sten_to_gll       ;
    auto &s2d2g              = this->sten_to_deriv_gll ;
    auto &c2g                = this->coefs_to_gll      ;
    auto &c2d2g              = this->coefs_to_deriv_gll;
    auto &derivMatrix        = this->derivMatrix       ;
    auto &fwaves             = this->fwaves            ;
    auto &surf_limits        = this->surf_limits       ;
    auto &grav               = this->grav              ;
    auto &gllWts_ngll        = this->gllWts_ngll       ;
    auto &idl                = this->idl               ;
    auto &sigma              = this->sigma             ;
    auto &wenoRecon          = this->wenoRecon         ;
    auto &sim1d              = this->sim1d             ;

    // x-direction boundaries
    parallel_for( Bounds<3>(numState,ny,hs) , YAKL_LAMBDA (int l, int j, int ii) {
      if        (bc_x == BC_WALL || bc_x == BC_OPEN) {
        state(l,hs+j,      ii) = state(l,hs+j,hs     );
        state(l,hs+j,nx+hs+ii) = state(l,hs+j,hs+nx-1);
        if (bc_x == BC_WALL && l == idU) {
          state(l,hs+j,      ii) = 0;
          state(l,hs+j,nx+hs+ii) = 0;
        }
      } else if (bc_x == BC_PERIODIC) {
        state(l,hs+j,      ii) = state(l,hs+j,nx+ii);
        state(l,hs+j,nx+hs+ii) = state(l,hs+j,hs+ii);
      }
    });

    // Loop over cells, reconstruct, compute time derivs, time average,
    // store state edge fluxes, compute cell-centered tendencies
    parallel_for( Bounds<2>(ny,nx) , YAKL_LAMBDA (int j, int i) {
      SArray<real,1,ord> stencil;

      // Reconstruct h and u
      SArray<real,2,nAder,ngll> h_DTs;
      SArray<real,2,nAder,ngll> u_DTs;
      SArray<real,2,nAder,ngll> v_DTs;
      SArray<real,2,nAder,ngll> dv_DTs;
      SArray<real,2,nAder,ngll> surf_DTs;
      for (int ii=0; ii<ord; ii++) { stencil(ii) = state(idH,hs+j,i+ii); }
      reconstruct_gll_values( stencil , h_DTs , true , s2g , c2g , idl , sigma , wenoRecon );

      for (int ii=0; ii<ord; ii++) { stencil(ii) = state(idU,hs+j,i+ii); }
      reconstruct_gll_values( stencil , u_DTs , true , s2g , c2g , idl , sigma , wenoRecon );
      if (bc_x == BC_WALL) {
        if (i == nx-1) u_DTs(0,ngll-1) = 0;
        if (i == 0   ) u_DTs(0,0     ) = 0;
      }

      for (int ii=0; ii<ord; ii++) { stencil(ii) = state(idV,hs+j,i+ii); }
      reconstruct_gll_values_and_derivs( stencil , v_DTs , dv_DTs, dx , true , s2g , s2d2g , c2g , c2d2g , idl , sigma , wenoRecon );

      for (int ii=0; ii<ord; ii++) { stencil(ii) = state(idH,hs+j,i+ii) + bath(hs+j,i+ii); }
      reconstruct_gll_values( stencil , surf_DTs , true , s2g , c2g , idl , sigma , wenoRecon );

      SArray<real,2,nAder,ngll> h_u_DTs;
      SArray<real,2,nAder,ngll> u_u_DTs;
      SArray<real,2,nAder,ngll> u_dv_DTs;
      for (int ii=0; ii<ngll; ii++) {
        h_u_DTs (0,ii) = h_DTs(0,ii) * u_DTs (0,ii);
        u_u_DTs (0,ii) = u_DTs(0,ii) * u_DTs (0,ii);
        u_dv_DTs(0,ii) = u_DTs(0,ii) * dv_DTs(0,ii);
      }

      if (nAder > 1) {
        for (int kt=0; kt < nAder-1; kt++) {
          // Compute state at kt+1
          for (int ii=0; ii<ngll; ii++) {
            // Compute d_dx(h*u) and d_dx(u*u/2+grav*(h+hb))
            real dh_u_dx  = 0;
            real dutend_dx  = 0;
            for (int s=0; s<ngll; s++) {
              dh_u_dx   += derivMatrix(s,ii) * h_u_DTs (kt,s);
              dutend_dx += derivMatrix(s,ii) * ( u_u_DTs(kt,s)/2 + grav*surf_DTs(kt,s) );
            }
            dh_u_dx   /= dx;
            dutend_dx /= dx;
            h_DTs(kt+1,ii) = -( dh_u_dx         ) / (kt+1);
            u_DTs(kt+1,ii) = -( dutend_dx       ) / (kt+1);
            v_DTs(kt+1,ii) = -( u_dv_DTs(kt,ii) ) / (kt+1);
          }
          if (bc_x == BC_WALL) {
            if (i == nx-1) u_DTs(kt+1,ngll-1) = 0;
            if (i == 0   ) u_DTs(kt+1,0     ) = 0;
          }
          // Compute h*u, u*u, h+b, and u_dv_DTs at kt+1
          for (int ii=0; ii<ngll; ii++) {
            // bathymetry has no time derivatives (no earthquakes...)
            surf_DTs(kt+1,ii) = h_DTs(kt+1,ii);
            // Differentiate v at kt+1 to get dv at kt+1
            real dv_dx = 0;
            for (int s=0; s<ngll; s++) {
              dv_dx += derivMatrix(s,ii) * v_DTs(kt+1,s);
            }
            dv_dx /= dx;
            dv_DTs(kt+1,ii) = dv_dx;
            // Compute h*u, u*u, and u*dv
            h_u_DTs (kt+1,ii) = 0;
            u_u_DTs (kt+1,ii) = 0;
            u_dv_DTs(kt+1,ii) = 0;
            for (int rt=0; rt <= kt+1; rt++) {
              h_u_DTs (kt+1,ii) += h_DTs(rt,ii) * u_DTs (kt+1-rt,ii);
              u_u_DTs (kt+1,ii) += u_DTs(rt,ii) * u_DTs (kt+1-rt,ii);
              u_dv_DTs(kt+1,ii) += u_DTs(rt,ii) * dv_DTs(kt+1-rt,ii);
            }
          }
        }
      }

      if (timeAvg) {
        // Compute time averages
        for (int ii=0; ii<ngll; ii++) {
          real dtmult = 1;
          real h_tavg    = 0;
          real u_tavg    = 0;
          real v_tavg    = 0;
          real surf_tavg = 0;
          real h_u_tavg  = 0;
          real u_u_tavg  = 0;
          real u_dv_tavg = 0;
          for (int kt=0; kt<nAder; kt++) {
            h_tavg      += h_DTs   (kt,ii) * dtmult / (kt+1);
            u_tavg      += u_DTs   (kt,ii) * dtmult / (kt+1);
            v_tavg      += v_DTs   (kt,ii) * dtmult / (kt+1);
            surf_tavg   += surf_DTs(kt,ii) * dtmult / (kt+1);
            h_u_tavg    += h_u_DTs (kt,ii) * dtmult / (kt+1);
            u_u_tavg    += u_u_DTs (kt,ii) * dtmult / (kt+1);
            u_dv_tavg   += u_dv_DTs(kt,ii) * dtmult / (kt+1);
            dtmult *= dt;
          }
          h_DTs   (0,ii) = h_tavg;
          u_DTs   (0,ii) = u_tavg;
          v_DTs   (0,ii) = v_tavg;
          surf_DTs(0,ii) = surf_tavg;
          h_u_DTs (0,ii) = h_u_tavg;
          u_u_DTs (0,ii) = u_u_tavg;
          u_dv_DTs(0,ii) = u_dv_tavg;
        }
      }

      // Store edge estimates of h and u into the fwaves object
      for (int kt=0; kt<nTimeDerivs; kt++) {
        fwaves (idH,kt,1,j,i  ) = h_DTs   (kt,0     );
        fwaves (idH,kt,0,j,i+1) = h_DTs   (kt,ngll-1);
        fwaves (idU,kt,1,j,i  ) = u_DTs   (kt,0     );
        fwaves (idU,kt,0,j,i+1) = u_DTs   (kt,ngll-1);
        fwaves (idV,kt,1,j,i  ) = v_DTs   (kt,0     );
        fwaves (idV,kt,0,j,i+1) = v_DTs   (kt,ngll-1);
        surf_limits(kt,1,j,i  ) = surf_DTs(kt,0     );
        surf_limits(kt,0,j,i+1) = surf_DTs(kt,ngll-1);

        // // Compute the "centered" contribution to the high-order tendency
        // // d_t(h) = - d_x(h*u)
        // tend(idH,kt,j,i) = - ( h_u_DTs(kt,ngll-1) - h_u_DTs(kt,0) ) / dx;
        // // d_t(u) = - d_x(u*u/2 + grav*(h+hb))
        // tend(idU,kt,j,i) = - ( ( u_u_DTs(kt,ngll-1)/2 + grav*surf_DTs(kt,ngll-1) ) -
        //                        ( u_u_DTs(kt,0     )/2 + grav*surf_DTs(kt,0     ) ) ) / dx;
        // d_t(v) + u*d_x(v) = 0 (must be integrated with GLL quadrature)
        if (! sim1d) {
          tend(idV,kt,j,i) = 0;
          for (int ii=0; ii<ngll; ii++) {
            tend(idV,kt,j,i) += -u_dv_DTs(kt,ii) * gllWts_ngll(ii);
          }
        }
      }

    }); // Loop over cells

    // Periodic BCs for fwaves and surf_limits
    parallel_for( Bounds<2>(nTimeDerivs,ny) , YAKL_LAMBDA (int kt, int j) {
      if (bc_x == BC_WALL || bc_x == BC_OPEN) {
        for (int l=0; l < numState; l++) {
          fwaves(l,kt,0,j,0 ) = fwaves(l,kt,1,j,0 );
          fwaves(l,kt,1,j,nx) = fwaves(l,kt,0,j,nx);
          if (bc_x == BC_WALL && l == idU) {
            fwaves(l,kt,0,j,0 ) = 0;
            fwaves(l,kt,1,j,0 ) = 0;
            fwaves(l,kt,0,j,nx) = 0;
            fwaves(l,kt,1,j,nx) = 0;
          }
          surf_limits(kt,0,j,0 ) = surf_limits(kt,1,j,0 );
          surf_limits(kt,1,j,nx) = surf_limits(kt,0,j,nx);
        }
      } else if (bc_x == BC_PERIODIC) {
        for (int l=0; l < numState; l++) {
          fwaves(l,kt,0,j,0 ) = fwaves(l,kt,0,j,nx);
          fwaves(l,kt,1,j,nx) = fwaves(l,kt,1,j,0 );
        }
        surf_limits(kt,0,j,0 ) = surf_limits(kt,0,j,nx);
        surf_limits(kt,1,j,nx) = surf_limits(kt,1,j,0 );
      }
    });

    // Split the flux difference into characteristic waves
    parallel_for( Bounds<2>(ny,nx+1) , YAKL_LAMBDA (int j, int i) {
      // State values for left and right
      real h_L  = fwaves     (idH,0,0,j,i);
      real u_L  = fwaves     (idU,0,0,j,i);
      real v_L  = fwaves     (idV,0,0,j,i);
      real hs_L = surf_limits(    0,0,j,i);  // Surface height
      real h_R  = fwaves     (idH,0,1,j,i);
      real u_R  = fwaves     (idU,0,1,j,i);
      real v_R  = fwaves     (idV,0,1,j,i);
      real hs_R = surf_limits(    0,1,j,i);  // Surface height
      // Compute interface linearly averaged values for the state
      real h = 0.5_fp * (h_L + h_R);
      real u = 0.5_fp * (u_L + u_R);
      real v = 0.5_fp * (v_L + v_R);
      real gw = sqrt(grav*h);
      // Store the kt-th time derivative
      for (int kt=0; kt < nTimeDerivs; kt++) {
        real h_L  = fwaves     (idH,kt,0,j,i);
        real u_L  = fwaves     (idU,kt,0,j,i);
        real v_L  = fwaves     (idV,kt,0,j,i);
        real hs_L = surf_limits(    kt,0,j,i);  // Surface height
        real h_R  = fwaves     (idH,kt,1,j,i);
        real u_R  = fwaves     (idU,kt,1,j,i);
        real v_R  = fwaves     (idV,kt,1,j,i);
        real hs_R = surf_limits(    kt,1,j,i);  // Surface height
        // Jump in the state
        real dh = h_R  - h_L ;
        real du = u_R  - u_L ;
        real dv = v_R  - v_L ;
        real dhs = hs_R - hs_L;
        // Jump in the flux: df = (df/dq)*dq
        real df1 = h_R*u_R - h_L*u_L;
        real df2 = u*du + grav*dhs;
        real df3 = u*dv;
        // l \dot df
        real w1 = 0.5_fp*df1 - h*df2/(2*gw);
        real w2 = 0.5_fp*df1 + h*df2/(2*gw);
        real w3 = df3;
        // set fwaves to zero for this interface
        for (int l=0; l < numState; l++) {
          fwaves(l,kt,0,j,i) = 0;
          fwaves(l,kt,1,j,i) = 0;
        }
        // Wave 1
        if (u-gw < 0) {
          fwaves(idH,kt,0,j,i) += w1;
          fwaves(idU,kt,0,j,i) += -gw*w1/h;
        } else {
          fwaves(idH,kt,1,j,i) += w1;
          fwaves(idU,kt,1,j,i) += -gw*w1/h;
        }
        // Wave 2
        if (u+gw < 0) {
          fwaves(idH,kt,0,j,i) += w2;
          fwaves(idU,kt,0,j,i) += gw*w2/h;
        } else {
          fwaves(idH,kt,1,j,i) += w2;
          fwaves(idU,kt,1,j,i) += gw*w2/h;
        }
        if (! sim1d) {
          if (u < 0) {
            fwaves(idV,kt,0,j,i) += w3;
          } else {
            fwaves(idV,kt,1,j,i) += w3;
          }
        }

        // We're going to replace fwaves for mass with a flux vector for easy mass conservation
        // Store in fwaves(idH,kt,0,j,i)
        real fwh_m = fwaves(idH,kt,0,j,i); // negative (minus) direction f-wave for h
        real fwh_p = fwaves(idH,kt,1,j,i); // positive (plus ) direction f-wave for h
        fwaves(idH,kt,0,j,i) = 0.5_fp * ( (h_L*u_L+fwh_m)  +  (h_R*u_R-fwh_p) );

        // We're going to replace fwaves for mass with a flux vector for easy mass conservation
        // Store in fwaves(idU,kt,0,j,i)
        fwh_m = fwaves(idU,kt,0,j,i); // negative (minus) direction f-wave for h
        fwh_p = fwaves(idU,kt,1,j,i); // positive (plus ) direction f-wave for h
        fwaves(idU,kt,0,j,i) = 0.5_fp * ( (u_L*u_L*0.5_fp + grav*hs_L + fwh_m)  + 
                                          (u_R*u_R*0.5_fp + grav*hs_R - fwh_p) );
      }
    });

    // Apply the tendencies
    parallel_for( Bounds<4>(numState,nTimeDerivs,ny,nx) , YAKL_LAMBDA (int l, int kt, int j, int i) {
      if (l == idH || l == idU) {
        tend(l,kt,j,i) = -( fwaves(l,kt,0,j,i+1) - fwaves(l,kt,0,j,i) ) / dx;
      } else {
        tend(l,kt,j,i) += -( fwaves(l,kt,1,j,i) + fwaves(l,kt,0,j,i+1) ) / dx;
      }
    });

  }



  // Compute state and tendency time derivatives from the state
  void computeTendenciesY( StateArr &state , TendArr &tend , real dt ) {
    auto &bc_y               = this->bc_y              ;
    auto &ny                 = this->ny                ;
    auto &dy                 = this->dy                ;
    auto &bath               = this->bath              ;
    auto &s2g                = this->sten_to_gll       ;
    auto &s2d2g              = this->sten_to_deriv_gll ;
    auto &c2g                = this->coefs_to_gll      ;
    auto &c2d2g              = this->coefs_to_deriv_gll;
    auto &derivMatrix        = this->derivMatrix       ;
    auto &fwaves             = this->fwaves            ;
    auto &surf_limits        = this->surf_limits       ;
    auto &grav               = this->grav              ;
    auto &gllWts_ngll        = this->gllWts_ngll       ;
    auto &idl                = this->idl               ;
    auto &sigma              = this->sigma             ;
    auto &wenoRecon          = this->wenoRecon         ;

    // x-direction boundaries
    parallel_for( Bounds<3>(numState,hs,nx) , YAKL_LAMBDA (int l, int jj, int i) {
      if        (bc_y == BC_WALL || bc_y == BC_OPEN) {
        state(l,      jj,hs+i) = state(l,hs     ,hs+i);
        state(l,ny+hs+jj,hs+i) = state(l,hs+ny-1,hs+i);
        if (bc_y == BC_WALL && l == idV) {
          state(l,      jj,hs+i) = 0;
          state(l,ny+hs+jj,hs+i) = 0;
        }
      } else if (bc_y == BC_PERIODIC) {
        state(l,      jj,hs+i) = state(l,ny+jj,hs+i);
        state(l,ny+hs+jj,hs+i) = state(l,hs+jj,hs+i);
      }
    });

    // Loop over cells, reconstruct, compute time derivs, time average,
    // store state edge fluxes, compute cell-centered tendencies
    parallel_for( Bounds<2>(ny,nx) , YAKL_LAMBDA (int j, int i) {
      SArray<real,1,ord> stencil;

      // Reconstruct h and u
      SArray<real,2,nAder,ngll> h_DTs;
      SArray<real,2,nAder,ngll> u_DTs;
      SArray<real,2,nAder,ngll> du_DTs;
      SArray<real,2,nAder,ngll> v_DTs;
      SArray<real,2,nAder,ngll> surf_DTs;
      for (int jj=0; jj<ord; jj++) { stencil(jj) = state(idH,j+jj,hs+i); }
      reconstruct_gll_values( stencil , h_DTs , true , s2g , c2g , idl , sigma , wenoRecon );

      for (int jj=0; jj<ord; jj++) { stencil(jj) = state(idU,j+jj,hs+i); }
      reconstruct_gll_values_and_derivs( stencil , u_DTs , du_DTs, dy , true , s2g , s2d2g , c2g , c2d2g , idl , sigma , wenoRecon );

      for (int jj=0; jj<ord; jj++) { stencil(jj) = state(idV,j+jj,hs+i); }
      reconstruct_gll_values( stencil , v_DTs , true , s2g , c2g , idl , sigma , wenoRecon );
      if (bc_y == BC_WALL) {
        if (j == ny-1) v_DTs(0,ngll-1) = 0;
        if (j == 0   ) v_DTs(0,0     ) = 0;
      }

      for (int jj=0; jj<ord; jj++) { stencil(jj) = state(idH,j+jj,hs+i) + bath(j+jj,hs+i); }
      reconstruct_gll_values( stencil , surf_DTs , true , s2g , c2g , idl , sigma , wenoRecon );

      SArray<real,2,nAder,ngll> h_v_DTs;
      SArray<real,2,nAder,ngll> v_du_DTs;
      SArray<real,2,nAder,ngll> v_v_DTs;
      for (int jj=0; jj<ngll; jj++) {
        h_v_DTs (0,jj) = h_DTs(0,jj) * v_DTs (0,jj);
        v_du_DTs(0,jj) = v_DTs(0,jj) * du_DTs(0,jj);
        v_v_DTs (0,jj) = v_DTs(0,jj) * v_DTs (0,jj);
      }

      if (nAder > 1) {
        for (int kt=0; kt < nAder-1; kt++) {
          // Compute state at kt+1
          for (int jj=0; jj<ngll; jj++) {
            // Compute d_dy(h*v) and d_dy(v*v/2+grav*(h+hb))
            real dh_v_dy  = 0;
            real dvtend_dy  = 0;
            for (int s=0; s<ngll; s++) {
              dh_v_dy   += derivMatrix(s,jj) * h_v_DTs(kt,s);
              dvtend_dy += derivMatrix(s,jj) * ( v_v_DTs(kt,s)/2 + grav*surf_DTs(kt,s) );
            }
            dh_v_dy   /= dy;
            dvtend_dy /= dy;
            h_DTs(kt+1,jj) = -( dh_v_dy         ) / (kt+1);
            u_DTs(kt+1,jj) = -( v_du_DTs(kt,jj) ) / (kt+1);
            v_DTs(kt+1,jj) = -( dvtend_dy       ) / (kt+1);
          }
          if (bc_y == BC_WALL) {
            if (j == ny-1) v_DTs(kt+1,ngll-1) = 0;
            if (j == 0   ) v_DTs(kt+1,0     ) = 0;
          }
          // Compute h*v, v*v, h+b, and v_du_DTs at kt+1
          for (int jj=0; jj<ngll; jj++) {
            // bathymetry has no time derivatives (no earthquakes...)
            surf_DTs(kt+1,jj) = h_DTs(kt+1,jj);
            // Differentiate v at kt+1 to get dv at kt+1
            real du_dy = 0;
            for (int s=0; s<ngll; s++) {
              du_dy += derivMatrix(s,jj) * u_DTs(kt+1,s);
            }
            du_dy /= dy;
            du_DTs(kt+1,jj) = du_dy;
            // Compute h*u, u*u, and u*dv
            h_v_DTs (kt+1,jj) = 0;
            v_v_DTs (kt+1,jj) = 0;
            v_du_DTs(kt+1,jj) = 0;
            for (int rt=0; rt <= kt+1; rt++) {
              h_v_DTs (kt+1,jj) += h_DTs(rt,jj) * v_DTs (kt+1-rt,jj);
              v_v_DTs (kt+1,jj) += v_DTs(rt,jj) * v_DTs (kt+1-rt,jj);
              v_du_DTs(kt+1,jj) += v_DTs(rt,jj) * du_DTs(kt+1-rt,jj);
            }
          }
        }
      }

      if (timeAvg) {
        // Compute time averages
        for (int ii=0; ii<ngll; ii++) {
          real dtmult = 1;
          real h_tavg    = 0;
          real u_tavg    = 0;
          real v_tavg    = 0;
          real surf_tavg = 0;
          real h_v_tavg  = 0;
          real v_v_tavg  = 0;
          real v_du_tavg = 0;
          for (int kt=0; kt<nAder; kt++) {
            h_tavg      += h_DTs   (kt,ii) * dtmult / (kt+1);
            u_tavg      += u_DTs   (kt,ii) * dtmult / (kt+1);
            v_tavg      += v_DTs   (kt,ii) * dtmult / (kt+1);
            surf_tavg   += surf_DTs(kt,ii) * dtmult / (kt+1);
            h_v_tavg    += h_v_DTs (kt,ii) * dtmult / (kt+1);
            v_v_tavg    += v_v_DTs (kt,ii) * dtmult / (kt+1);
            v_du_tavg   += v_du_DTs(kt,ii) * dtmult / (kt+1);
            dtmult *= dt;
          }
          h_DTs   (0,ii) = h_tavg;
          u_DTs   (0,ii) = u_tavg;
          v_DTs   (0,ii) = v_tavg;
          surf_DTs(0,ii) = surf_tavg;
          h_v_DTs (0,ii) = h_v_tavg;
          v_v_DTs (0,ii) = v_v_tavg;
          v_du_DTs(0,ii) = v_du_tavg;
        }
      }

      // Store edge estimates of h and u into the fwaves object
      for (int kt=0; kt<nTimeDerivs; kt++) {
        fwaves (idH,kt,1,j  ,i) = h_DTs   (kt,0     );
        fwaves (idH,kt,0,j+1,i) = h_DTs   (kt,ngll-1);
        fwaves (idU,kt,1,j  ,i) = u_DTs   (kt,0     );
        fwaves (idU,kt,0,j+1,i) = u_DTs   (kt,ngll-1);
        fwaves (idV,kt,1,j  ,i) = v_DTs   (kt,0     );
        fwaves (idV,kt,0,j+1,i) = v_DTs   (kt,ngll-1);
        surf_limits(kt,1,j  ,i) = surf_DTs(kt,0     );
        surf_limits(kt,0,j+1,i) = surf_DTs(kt,ngll-1);

        // // Compute the "centered" contribution to the high-order tendency
        // // d_t(h) = - d_y(h*v)
        // tend(idH,kt,j,i) = - ( h_v_DTs(kt,ngll-1) - h_v_DTs(kt,0) ) / dy;
        // // d_t(v) = - d_y(v*v/2 + grav*(h+hb))
        // tend(idV,kt,j,i) = - ( ( v_v_DTs(kt,ngll-1)/2 + grav*surf_DTs(kt,ngll-1) ) -
        //                        ( v_v_DTs(kt,0     )/2 + grav*surf_DTs(kt,0     ) ) ) / dy;
        // d_t(u) + v*d_y(u) = 0 (must be integrated with GLL quadrature)
        tend(idU,kt,j,i) = 0;
        for (int ii=0; ii<ngll; ii++) {
          tend(idU,kt,j,i) += -v_du_DTs(kt,ii) * gllWts_ngll(ii);
        }
      }

    }); // Loop over cells

    // Periodic BCs for fwaves and surf_limits
    parallel_for( Bounds<2>(nTimeDerivs,nx) , YAKL_LAMBDA (int kt, int i) {
      if (bc_y == BC_WALL || bc_y == BC_OPEN) {
        for (int l=0; l < numState; l++) {
          fwaves(l,kt,0,0 ,i) = fwaves(l,kt,1,0 ,i);
          fwaves(l,kt,1,ny,i) = fwaves(l,kt,0,ny,i);
          if (bc_y == BC_WALL && l == idV) {
            fwaves(l,kt,0,0 ,i) = 0;
            fwaves(l,kt,1,0 ,i) = 0;
            fwaves(l,kt,0,ny,i) = 0;
            fwaves(l,kt,1,ny,i) = 0;
          }
          surf_limits(kt,0,0 ,i) = surf_limits(kt,1,0 ,i);
          surf_limits(kt,1,ny,i) = surf_limits(kt,0,ny,i);
        }
      } else if (bc_y == BC_PERIODIC) {
        for (int l=0; l < numState; l++) {
          fwaves(l,kt,0,0 ,i) = fwaves(l,kt,0,ny,i);
          fwaves(l,kt,1,ny,i) = fwaves(l,kt,1,0 ,i);
        }
        surf_limits(kt,0,0 ,i) = surf_limits(kt,0,ny,i);
        surf_limits(kt,1,ny,i) = surf_limits(kt,1,0 ,i);
      }
    });

    // Split the flux difference into characteristic waves
    parallel_for( Bounds<2>(ny+1,nx) , YAKL_LAMBDA (int j, int i) {
      // State values for left and right
      real h_L  = fwaves     (idH,0,0,j,i);
      real u_L  = fwaves     (idU,0,0,j,i);
      real v_L  = fwaves     (idV,0,0,j,i);
      real hs_L = surf_limits(    0,0,j,i);  // Surface height
      real h_R  = fwaves     (idH,0,1,j,i);
      real u_R  = fwaves     (idU,0,1,j,i);
      real v_R  = fwaves     (idV,0,1,j,i);
      real hs_R = surf_limits(    0,1,j,i);  // Surface height
      // Compute interface linearly averaged values for the state
      real h = 0.5_fp * (h_L + h_R);
      real u = 0.5_fp * (u_L + u_R);
      real v = 0.5_fp * (v_L + v_R);
      real gw = sqrt(grav*h);
      // Store the kt-th time derivative
      for (int kt=0; kt < nTimeDerivs; kt++) {
        real h_L  = fwaves     (idH,kt,0,j,i);
        real u_L  = fwaves     (idU,kt,0,j,i);
        real v_L  = fwaves     (idV,kt,0,j,i);
        real hs_L = surf_limits(    kt,0,j,i);  // Surface height
        real h_R  = fwaves     (idH,kt,1,j,i);
        real u_R  = fwaves     (idU,kt,1,j,i);
        real v_R  = fwaves     (idV,kt,1,j,i);
        real hs_R = surf_limits(    kt,1,j,i);  // Surface height
        // Jump in the state
        real dh = h_R  - h_L ;
        real du = u_R  - u_L ;
        real dv = v_R  - v_L ;
        real dhs = hs_R - hs_L;
        // Jump in the flux: df = (df/dq)*dq
        real df1 = h_R*v_R - h_L*v_L;
        real df2 = v*du;
        real df3 = v*dv + grav*dhs;
        // l \dot df
        real w1 = 0.5_fp*df1 - h*df3/(2*gw);
        real w2 = 0.5_fp*df1 + h*df3/(2*gw);
        real w3 = df2;
        // set fwaves to zero for this interface
        for (int l=0; l < numState; l++) {
          fwaves(l,kt,0,j,i) = 0;
          fwaves(l,kt,1,j,i) = 0;
        }
        // Wave 1
        if (v-gw < 0) {
          fwaves(idH,kt,0,j,i) += w1;
          fwaves(idV,kt,0,j,i) += -gw*w1/h;
        } else {
          fwaves(idH,kt,1,j,i) += w1;
          fwaves(idV,kt,1,j,i) += -gw*w1/h;
        }
        // Wave 2
        if (v+gw < 0) {
          fwaves(idH,kt,0,j,i) += w2;
          fwaves(idV,kt,0,j,i) += gw*w2/h;
        } else {
          fwaves(idH,kt,1,j,i) += w2;
          fwaves(idV,kt,1,j,i) += gw*w2/h;
        }
        if (v < 0) {
          fwaves(idU,kt,0,j,i) += w3;
        } else {
          fwaves(idU,kt,1,j,i) += w3;
        }

        // We're going to replace fwaves for mass with a flux vector for easy mass conservation
        // Store in fwaves(idH,kt,0,j,i)
        real fwh_m = fwaves(idH,kt,0,j,i); // negative (minus) direction f-wave for h
        real fwh_p = fwaves(idH,kt,1,j,i); // positive (plus ) direction f-wave for h
        fwaves(idH,kt,0,j,i) = 0.5_fp * ( (h_L*v_L+fwh_m)  +  (h_R*v_R-fwh_p) );

        // We're going to replace fwaves for mass with a flux vector for easy mass conservation
        // Store in fwaves(idH,kt,0,j,i)
        fwh_m = fwaves(idV,kt,0,j,i); // negative (minus) direction f-wave for h
        fwh_p = fwaves(idV,kt,1,j,i); // positive (plus ) direction f-wave for h
        fwaves(idV,kt,0,j,i) = 0.5_fp * ( (v_L*v_L*0.5_fp + grav*hs_L + fwh_m)  + 
                                          (v_R*v_R*0.5_fp + grav*hs_R - fwh_p) );
      }
    });

    // Apply the tendencies
    parallel_for( Bounds<4>(numState,nTimeDerivs,ny,nx) , YAKL_LAMBDA (int l, int kt, int j, int i) {
      if (l == idH || l == idV) {
        tend(l,kt,j,i) = -( fwaves(l,kt,0,j+1,i) - fwaves(l,kt,0,j,i) ) / dy;
      } else {
        tend(l,kt,j,i) += -( fwaves(l,kt,1,j,i) + fwaves(l,kt,0,j+1,i) ) / dy;
      }
    });

  }



  template <class F> void applyTendencies( F const &applySingleTendency , int splitIndex ) {
    parallel_for( Bounds<3>(numState,ny,nx) , YAKL_LAMBDA (int l, int j, int i) {
      applySingleTendency({l,j,i});
    });
  }



  const char * getSpatialName() { return "1-D Uniform Transport with upwind FV on A-grid"; }



  void output(StateArr const &state, real etime) {
    auto &bath = this->bath;

    yakl::SimpleNetCDF nc;
    int ulIndex = 0; // Unlimited dimension index to place this data at

    // Create or open the file
    if (etime == 0.) {
      auto &dx   = this->dx;
      auto &dy   = this->dy;

      nc.create(outFile);

      // Create spatial variables
      real1d xloc("xloc",nx);
      parallel_for( nx , YAKL_LAMBDA (int i) { xloc(i) = (i+0.5)*dx; });
      nc.write(xloc.createHostCopy(),"x",{"x"});

      real1d yloc("yloc",ny);
      parallel_for( ny , YAKL_LAMBDA (int j) { yloc(j) = (j+0.5)*dy; });
      nc.write(yloc.createHostCopy(),"y",{"y"});

      // Write bathymetry data
      real2d data("data",ny,nx);
      parallel_for( Bounds<2>(ny,nx) , YAKL_LAMBDA (int j, int i) { data(j,i) = bath(hs+j,hs+i); });
      nc.write(data.createHostCopy(),"bath",{"y","x"});

      // Elapsed time
      nc.write1(0._fp,"t",0,"t");
    } else {
      nc.open(outFile,yakl::NETCDF_MODE_WRITE);

      // Write the elapsed time
      ulIndex = nc.getDimSize("t");
      nc.write1(etime,"t",ulIndex,"t");
    }
    // Write the data
    real2d data("data",ny,nx);
    parallel_for( Bounds<2>(ny,nx) , YAKL_LAMBDA (int j, int i) { data(j,i) = state(idH,hs+j,hs+i); });
    nc.write1(data.createHostCopy(),"thickness",{"y","x"},ulIndex,"t");

    parallel_for( Bounds<2>(ny,nx) , YAKL_LAMBDA (int j, int i) { data(j,i) = state(idU,hs+j,hs+i); });
    nc.write1(data.createHostCopy(),"u",{"y","x"},ulIndex,"t");

    parallel_for( Bounds<2>(ny,nx) , YAKL_LAMBDA (int j, int i) { data(j,i) = state(idV,hs+j,hs+i); });
    nc.write1(data.createHostCopy(),"v",{"y","x"},ulIndex,"t");

    parallel_for( Bounds<2>(ny,nx) , YAKL_LAMBDA (int j, int i) { data(j,i) = state(idH,hs+j,hs+i) + bath(hs+j,hs+i); });
    nc.write1(data.createHostCopy(),"surface"  ,{"y","x"},ulIndex,"t");

    // Close the file
    nc.close();
  }



  void finalize(StateArr const &state) {
    real2d mass("mass",ny,nx);
    parallel_for( Bounds<2>(ny,nx) , YAKL_LAMBDA (int j, int i) {
      real h = state(idH,hs+j,hs+i);
      mass(j,i) = h;
    });
    yakl::ParallelSum<real,memDevice> psum(ny*nx);
    real mass_tot = psum(mass.data());

    std::cout << "Relative mass change: " << (mass_tot-mass_init) / mass_init << "\n";
  }



  // ord stencil values to ngll GLL values and ngll GLL derivatives; store in DTs
  YAKL_INLINE void reconstruct_gll_values_and_derivs( SArray<real,1,ord> const &stencil , SArray<real,2,nAder,ngll> &DTs ,
                                                      SArray<real,2,nAder,ngll> &deriv_DTs, real dx , bool doweno  ,
                                                      SArray<real,2,ord,ngll> const &s2g , SArray<real,2,ord,ngll> const &s2d2g ,
                                                      SArray<real,2,ord,ngll> const &c2g , SArray<real,2,ord,ngll> const &c2d2g ,
                                                      SArray<real,1,hs+2> const &idl , real sigma ,
                                                      SArray<real,3,ord,ord,ord> const &wenoRecon ) {
    if (doweno) {

      // Reconstruct values
      SArray<real,1,ord> wenoCoefs;
      weno::compute_weno_coefs( wenoRecon , stencil , wenoCoefs , idl , sigma );
      // Transform ord weno coefficients into ngll GLL points
      for (int ii=0; ii<ngll; ii++) {
        real tmp       = 0;
        real deriv_tmp = 0;
        for (int s=0; s < ord; s++) {
          real coef = wenoCoefs(s);
          tmp       += c2g  (s,ii) * coef;
          deriv_tmp += c2d2g(s,ii) * coef;
        }
        DTs      (0,ii) = tmp;
        deriv_DTs(0,ii) = deriv_tmp / dx;
      }

    } else {

      // Transform ord stencil cell averages into ngll GLL points
      for (int ii=0; ii<ngll; ii++) {
        real tmp       = 0;
        real deriv_tmp = 0;
        for (int s=0; s < ord; s++) {
          real sten = stencil(s);
          tmp       += s2g  (s,ii) * sten;
          deriv_tmp += s2d2g(s,ii) * sten;
        }
        DTs      (0,ii) = tmp;
        deriv_DTs(0,ii) = deriv_tmp / dx;
      }

    } // if doweno
  }



  // ord stencil values to ngll GLL values; store in DTs
  YAKL_INLINE void reconstruct_gll_values( SArray<real,1,ord> const stencil , SArray<real,2,nAder,ngll> &DTs , bool doweno ,
                                           SArray<real,2,ord,ngll> const &s2g , SArray<real,2,ord,ngll> const &c2g ,
                                           SArray<real,1,hs+2> const &idl , real sigma ,
                                           SArray<real,3,ord,ord,ord> const &wenoRecon ) {
    if (doweno) {

      // Reconstruct values
      SArray<real,1,ord> wenoCoefs;
      weno::compute_weno_coefs( wenoRecon , stencil , wenoCoefs , idl , sigma );
      // Transform ord weno coefficients into ngll GLL points
      for (int ii=0; ii<ngll; ii++) {
        real tmp = 0;
        for (int s=0; s < ord; s++) {
          tmp += c2g(s,ii) * wenoCoefs(s);
        }
        DTs(0,ii) = tmp;
      }

    } else {

      // Transform ord stencil cell averages into ngll GLL points
      for (int ii=0; ii<ngll; ii++) {
        real tmp = 0;
        for (int s=0; s < ord; s++) {
          tmp += s2g(s,ii) * stencil(s);
        }
        DTs(0,ii) = tmp;
      }

    } // if doweno
  }


};