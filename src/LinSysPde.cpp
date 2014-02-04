//C++ include
#include <ctime>
#include <fstream>
#include <algorithm>
#include "LinSysPde.hpp"
#include "ElemType.hpp"
#include "ParalleltypeEnum.hpp"
#include "NumericVector.hpp"
#include "PetscVector.hpp"
#include "SparseRectangularMatrix.hpp"
#include "PetscRectangularMatrix.hpp"

using std::cout;
using std::endl;

//--------------------------------------------------------------------------------
lsysPde::lsysPde(mesh *other_msh){    
  _msh = other_msh;
  _is_symmetric = false;
  _stabilization = false;
  _compressibility = 0.;
  CC_flag=0;
}

//--------------------------------------------------------------------------------
lsysPde::~lsysPde() { }

//--------------------------------------------------------------------------------
void lsysPde::SetMatrixProperties(const bool property) {
  _is_symmetric = property;
}

//--------------------------------------------------------------------------------
bool lsysPde::GetMatrixProperties() {
  return _is_symmetric;
}

//--------------------------------------------------------------------------------
void lsysPde::AddStabilization(const bool stab, const double compressibility) {
  _stabilization = stab;
  _compressibility = compressibility;
}

//--------------------------------------------------------------------------------
double lsysPde::GetCompressibility() {
  return _compressibility;
};

//--------------------------------------------------------------------------------
bool lsysPde::GetStabilization() {
  return _stabilization;
};

//--------------------------------------------------------------------------------
unsigned lsysPde::GetIndex(const char name[]) {
  unsigned index=0;
  while (strcmp(_SolName[index],name)) {
    index++;
    if (index==_SolType.size()) {
      cout<<"error! invalid name entry GetIndex(...)"<<endl;
      exit(0);
    }
  }
  return index;
}

//--------------------------------------------------------------------------------
int lsysPde::InitPde(const vector <unsigned> &_SolPdeIndex, const  vector <int> &SolType_other,  
		     const vector <char*> &SolName_other, vector <NumericVector*> *Bdc_other ) {
   
  _SolType=SolType_other;
  _SolName=SolName_other;
  _Bdc=Bdc_other;
  
  int ierr;
  KKIndex.resize(_SolPdeIndex.size()+1u);
  KKIndex[0]=0;
  for (unsigned i=1; i<KKIndex.size(); i++)
  KKIndex[i]=KKIndex[i-1]+_msh->MetisOffset[_SolType[_SolPdeIndex[i-1]]][_msh->_nprocs];

  //-----------------------------------------------------------------------------------------------
  KKoffset.resize(_SolPdeIndex.size()+1);
  for(int i=0;i<_SolPdeIndex.size()+1;i++) {
    KKoffset[i].resize(_msh->nsubdom);
  }
  
  KKoffset[0][0]=0;
  for(int j=1; j<_SolPdeIndex.size()+1; j++) {
    unsigned indexSol=_SolPdeIndex[j-1];
    KKoffset[j][0] = KKoffset[j-1][0]+(_msh->MetisOffset[_SolType[indexSol]][1] - _msh->MetisOffset[_SolType[indexSol]][0]);
  }
  
  for(int i=1; i<_msh->nsubdom; i++) {
    KKoffset[0][i] = KKoffset[_SolPdeIndex.size()][i-1];
    for(int j=1; j<_SolPdeIndex.size()+1; j++) {
      unsigned indexSol=_SolPdeIndex[j-1];
      KKoffset[j][i] = KKoffset[j-1][i]+(_msh->MetisOffset[_SolType[indexSol]][i+1] - _msh->MetisOffset[_SolType[indexSol]][i]);
    }
  }
   
  //ghost size
  KKghostsize.resize(_msh->nsubdom,0);
  for(int i=0; i<_msh->nsubdom; i++) {
    for(int j=0; j<_SolPdeIndex.size(); j++) {
      unsigned indexSol=_SolPdeIndex[j];
      KKghostsize[i] += _msh->ghost_size[_SolType[indexSol]][i];
    }
  }
  
  //ghost nodes
  KKghost_nd.resize(_msh->nsubdom);
  KKghost_nd[0].resize(1);  KKghost_nd[0][0]=1;
  for(int i=1; i<_msh->nsubdom; i++) {
    KKghost_nd[i].resize(KKghostsize[i]);
  }
  
  
  for(int i=0; i<_msh->nsubdom; i++) {
    unsigned counter=0;
    for(int j=0; j<_SolPdeIndex.size(); j++) {
       unsigned indexSol=_SolPdeIndex[j];
       for(int k=0; k<_msh->ghost_size[_SolType[indexSol]][i];k++) {
	 //gambit ghost node
	 unsigned gmt_ghost_nd = _msh->ghost_nd[_SolType[indexSol]][i][k];
	 KKghost_nd[i][counter] =  GetKKDof(indexSol,j,gmt_ghost_nd);
	 counter++;
       }
     }
   }
  
  //--------------------------------------------------------------------------------------
  DrchKKdofs.resize(KKoffset[KKIndex.size()-1][_msh->_iproc] - KKoffset[0][_msh->_iproc]);
  unsigned counter=0;
  for(int k=0; k<_SolPdeIndex.size(); k++) {
    unsigned indexSol=_SolPdeIndex[k];
    unsigned soltype=_SolType[indexSol];
    if(soltype<3) {
      for(unsigned inode_mts=_msh->MetisOffset[soltype][_msh->_iproc]; 
	 inode_mts<_msh->MetisOffset[soltype][_msh->_iproc+1]; inode_mts++) {
	 if((*(*_Bdc)[indexSol])(inode_mts)<1.9) {
	   int local_mts = inode_mts-_msh->MetisOffset[soltype][_msh->_iproc];
	   int idof_kk = KKoffset[k][_msh->_iproc] +local_mts; 
	   DrchKKdofs[counter]=idof_kk;
	   counter++;
	 }
      }
    }
  } 
  DrchKKdofs.resize(counter);
 
  //-----------------------------------------------------------------------------------------------
  int EPSsize= KKIndex[KKIndex.size()-1];
  _EPS = NumericVector::build().release();
  if(_msh->_nprocs==1) { // IF SERIAL
    _EPS->init(EPSsize,EPSsize,false,SERIAL);
  } 
  else { // IF PARALLEL
    int EPS_local_size =KKoffset[KKIndex.size()-1][_msh->_iproc] - KKoffset[0][_msh->_iproc];
    _EPS->init(EPSsize,EPS_local_size, KKghost_nd[_msh->_iproc], false,GHOSTED);
  }
    
//   _RES = NumericVector::build().release();
//   _RES->init(*_EPS);
//   
//   _EPSC = NumericVector::build().release();
//   _EPSC->init(*_EPS);
//   
//   _RESC = NumericVector::build().release();
//   _RESC->init(*_EPS);
//   
//   const unsigned dim = _msh->GetDimension();
//   int KK_UNIT_SIZE_ = pow(5,dim);
//   int KK_size=KKIndex[KKIndex.size()-1u];
//   int KK_local_size =KKoffset[KKIndex.size()-1][_msh->_iproc] - KKoffset[0][_msh->_iproc];
//     
//  // _KK = SparseRectangularMatrix::build().release();
//  // _KK->init(KK_size,KK_size,KK_local_size,KK_local_size,KK_UNIT_SIZE_*KKIndex.size(),KK_UNIT_SIZE_*KKIndex.size());
//   
//   
//   
// //   PetscVector* EPSp=static_cast<PetscVector*> (_EPS);  //TODO
// //   EPS=EPSp->vec(); //TODO
//   PetscVector* RESp=static_cast<PetscVector*> (_RES);  //TODO
//   RES=RESp->vec(); //TODO
//  
//   //PetscRectangularMatrix* KKp=static_cast<PetscRectangularMatrix*>(_KK); //TODO
//   //KK=KKp->mat(); //TODO
    
  return 1;
}

//--------------------------------------------------------------------------------
void lsysPde::SetResZero() {
  _RES->zero();
}

//--------------------------------------------------------------------------------
void lsysPde::SetEpsZero() {
  _EPS->zero();
  _EPSC->zero();
}

//--------------------------------------------------------------------------------
void lsysPde::SumEpsCToEps() {
  *_EPS += *_EPSC;
}

//--------------------------------------------------------------------------------
void lsysPde::UpdateResidual() {
  _RESC->matrix_mult(*_EPSC,*_KK);
  *_RES -= *_RESC;
}

//-------------------------------------------------------------------------------------------
void lsysPde::DeletePde() {
  delete _KK;
  
  if (_msh->GetGridNumber()>0) {
     delete _PP;
  }
  
  delete _EPS;
  delete _EPSC;
  delete _RES;
  delete _RESC;
  
}
//-------------------------------------------------------------------------------------------


