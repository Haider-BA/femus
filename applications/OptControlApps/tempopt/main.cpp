//C++ includes 
#include <sys/stat.h>
#include <sys/types.h>
#include <iostream>
#include <cstdlib>
#include <sstream>

// External library include ( LibMesh, PETSc...) ------------------------------
#include "FEMTTUConfig.h"

// FEMuS
#include "paral.hpp" 
#include "FemusInit.hpp"
#include "Files.hpp"
#include "MultiLevelMeshTwo.hpp"
#include "GenCase.hpp"
#include "FETypeEnum.hpp"
#include "GaussPoints.hpp"
#include "MultiLevelProblemTwo.hpp"
#include "ElemType.hpp"
#include "TimeLoop.hpp"
#include "Typedefs.hpp"
#include "Quantity.hpp"
#include "QTYnumEnum.hpp"
#include "Box.hpp"  //for the DOMAIN
#include "XDMFWriter.hpp"

// application 
#include "Temp_conf.hpp"
#include "TempQuantities.hpp"
#include "EqnNS.hpp"
#include "EqnT.hpp"
#include "OptLoop.hpp"


#ifdef HAVE_LIBMESH
#include "libmesh/libmesh.h"
#endif

// =======================================
// TEMPERATURE + NS optimal control problem
// ======================================= 

 int main(int argc, char** argv) {

#ifdef HAVE_LIBMESH
   libMesh::LibMeshInit init(argc,argv);
#else   
   FemusInit init(argc,argv);
#endif
   
 // ======= Files ========================
  Files files; 
        files.ConfigureRestart();
        files.CheckIODirectories();
        files.CopyInputFiles();
        files.RedirectCout();

  // ======= Physics Input Parser ========================
  FemusInputParser<double> physics_map("Physics",files.GetOutputPath());

  const double rhof   = physics_map.get("rho0");
  const double Uref   = physics_map.get("Uref");
  const double Lref   = physics_map.get("Lref");
  const double  muf   = physics_map.get("mu0");

  const double  _pref = rhof*Uref*Uref;           physics_map.set("pref",_pref);
  const double   _Re  = (rhof*Uref*Lref)/muf;     physics_map.set("Re",_Re);
  const double   _Fr  = (Uref*Uref)/(9.81*Lref);  physics_map.set("Fr",_Fr);
  const double   _Pr  = muf/rhof;                 physics_map.set("Pr",_Pr);

  // ======= Mesh =====
  FemusInputParser<double> mesh_map("Mesh",files.GetOutputPath());
  GenCase mesh(mesh_map,"inclQ2D2x2.gam");
          mesh.SetLref(1.);  
	  
  // ======= MyDomainShape  (optional, implemented as child of Domain) ====================
  FemusInputParser<double> box_map("Box",files.GetOutputPath());
  Box mybox(mesh.get_dim(),box_map);
      mybox.InitAndNondimensionalize(mesh.get_Lref());

          mesh.SetDomain(&mybox);    
	  
          mesh.GenerateCase(files.GetOutputPath());

          mesh.SetLref(Lref);
      mybox.InitAndNondimensionalize(mesh.get_Lref());
	  
          XDMFWriter::ReadMeshFileAndNondimensionalizeBiquadratic(files.GetOutputPath(),mesh); 
	  XDMFWriter::PrintMeshBiquadraticXDMF(files.GetOutputPath(),mesh);
          XDMFWriter::PrintMeshLinear(files.GetOutputPath(),mesh);
	  
  //gencase is dimensionalized, meshtwo is nondimensionalized
  //since the meshtwo is nondimensionalized, all the BC and IC are gonna be implemented on a nondimensionalized mesh
  //now, a mesh may or may not have an associated domain
  //moreover, a mesh may or may not be read from file
  //the generation is DIMENSIONAL, the nondimensionalization occurs later
  //Both the Mesh and the optional domain must be nondimensionalized
  //first, we have to say if the mesh has a shape or not
  //that depends on the application, it must be put at the main level
  //then, after you know the shape, you may or may not generate the mesh with that shape 
  //the two things are totally independent, and related to the application, not to the library

  // ======== Loop ===================================
  FemusInputParser<double> loop_map("TimeLoop",files.GetOutputPath());
  OptLoop opt_loop(files, loop_map); 
   
  // ===== QuantityMap : this is like the MultilevelSolution =========================================
  QuantityMap  qty_map(mesh,&physics_map);

  Temperature temperature("Qty_Temperature",qty_map,1,FE_TEMPERATURE);          qty_map.set_qty(&temperature);
  TempLift       templift("Qty_TempLift",qty_map,1,FE_TEMPERATURE,opt_loop);    qty_map.set_qty(&templift);  
  TempAdj         tempadj("Qty_TempAdj",qty_map,1,FE_TEMPERATURE);              qty_map.set_qty(&tempadj);  
  TempDes         tempdes("Qty_TempDes",qty_map,1,FE_TEMPERATURE);              qty_map.set_qty(&tempdes);  
  Pressure       pressure("Qty_Pressure",qty_map,1,FE_PRESSURE);                qty_map.set_qty(&pressure);
  Velocity       velocity("Qty_Velocity",qty_map,mesh.get_dim(),FE_VELOCITY);   qty_map.set_qty(&velocity);  

#if FOURTH_ROW==1
  Pressure2 pressure_2("Qty_Pressure_2",qty_map,1,T4_ORD);            qty_map.set_qty(&pressure_2);
#endif 
  
  // ===== end QuantityMap =========================================

  // ====== MultiLevelProblemTwo =================================
  MultiLevelProblemTwo equations_map(physics_map,qty_map,mesh,"fifth");  //here everything is passed as BASE STUFF, like it should!
                                                                                   //the equations need: physical parameters, physical quantities, Domain, FE, QRule, Time discretization  
  MultiLevelProblem equations_map_two;
//===============================================
//================== Add EQUATIONS AND ======================
//========= associate an EQUATION to QUANTITIES ========
//========================================================
// not all the Quantities need an association with equation
//once you associate one quantity in the internal map of an equation, then it is immediately to be associated to that equation,
//   so this operation of set_eqn could be done right away in the moment when you put the quantity in the equation
 
#if NS_EQUATIONS==1
  //to retrieve a quantity i can take it from the qtymap of the problem  //but here, in the main, i can take that quantity directly...
// std::vector<Quantity*> InternalVect_NS;
// InternalVect_NS.push_back(&velocity);      
// InternalVect_NS.push_back(&pressure);        
std::vector<Quantity*> InternalVect_NS(2); 
InternalVect_NS[0] = &velocity;  velocity.SetPosInAssocEqn(0);
InternalVect_NS[1] = &pressure;  pressure.SetPosInAssocEqn(1);

  EqnNS* eqnNS = new EqnNS(equations_map,"Eqn_NS",0,NO_SMOOTHER);  equations_map.add_system(eqnNS);
  eqnNS->SetQtyIntVector(InternalVect_NS);
//   equations_map_two.add_system("Basic","equazioneNS");
  
           velocity.set_eqn(eqnNS);
           pressure.set_eqn(eqnNS);
#endif
  
#if T_EQUATIONS==1
std::vector<Quantity*> InternalVect_Temp( 3 + FOURTH_ROW );  //of course this must be exactly equal to the following number of get_qty
                                                             // can I do this dynamic? 
                                                             // well, the following order is essential, because it is the same order 
                                                             // as the BLOCKS in the MATRIX, but at least with add you can avoid setting also the SIZE explicitly.
                                                             //The order in which you put the push_back instructions is essential and it gives you 
                                                             //the order in the std::vector!!!
InternalVect_Temp[0] = &temperature;       temperature.SetPosInAssocEqn(0);
InternalVect_Temp[1] = &templift;             templift.SetPosInAssocEqn(1);
InternalVect_Temp[2] = &tempadj;               tempadj.SetPosInAssocEqn(2);

#if FOURTH_ROW==1
InternalVect_Temp[3] = &pressure_2;         pressure_2.SetPosInAssocEqn(3);
#endif

  EqnT* eqnT = new EqnT(equations_map,"Eqn_T",1,NO_SMOOTHER);
  eqnT->SetQtyIntVector(InternalVect_Temp);

  equations_map.add_system(eqnT); 
  
        temperature.set_eqn(eqnT);
           templift.set_eqn(eqnT);
            tempadj.set_eqn(eqnT);
   #if FOURTH_ROW==1
         pressure_2.set_eqn(eqnT);
   #endif

#endif   

//================================ 
//========= End add EQUATIONS  and ========
//========= associate an EQUATION to QUANTITIES ========
//================================

//Ok now that the mesh file is there i want to COMPUTE the MG OPERATORS... but I want to compute them ONCE and FOR ALL,
//not for every equation... but the functions belong to the single equation... I need to make them EXTERNAL
// then I'll have A from the equation, PRL and REST from a MG object.
//So somehow i'll have to put these objects at a higher level... but so far let us see if we can COMPUTE and PRINT from HERE and not from the gencase
	 
 
//once you have the list of the equations, you loop over them to initialize everything

   for (MultiLevelProblemTwo::const_iterator eqn = equations_map.begin(); eqn != equations_map.end(); eqn++) {
        SystemTwo* mgsol = eqn->second;
        
//=====================
    mgsol -> _dofmap.ComputeMeshToDof();
//=====================
    mgsol -> GenerateBdc();
    mgsol -> GenerateBdcElem();
//=====================
    mgsol -> ReadMGOps(files.GetOutputPath());
//=====================
    mgsol -> initVectors();     //TODO can I do it earlier than this position?
//=====================
    mgsol -> Initialize();
    } 
	 
	 
  opt_loop.TransientSetup(equations_map);  // reset the initial state (if restart) and print the Case

  opt_loop.optimization_loop(equations_map);

// at this point, the run has been completed 
  files.PrintRunForRestart(DEFAULT_LAST_RUN);/*(iproc==0)*/  //============= prepare default for next restart ==========  
  files.log_petsc();
  
// ============  clean ================================
  equations_map.clean();
  mesh.clear();
  
  
  return 0;
}
