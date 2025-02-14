/* OIS: Orthogonal Interacting Sequences (2022) 
   Author: Duccio Malinverni, St.Jude Children's Research Hospital, Memphis, TN, USA 
   This file is covered by GPL-3.0 license (see LICENSE file in the root of this project.    
*/

#include "OrthoGenerator.h"

void ORT_generate(Samples native, Parameters params, PottsModel model){

  //Setup random number generator
  std::mt19937 rndGenerator;
  if(params.randomSeed==0)
    rndGenerator = std::mt19937(time(NULL));
  else
    rndGenerator = std::mt19937(params.randomSeed);
  
  // Build mutable positions list
  std::vector<uInt> mutatePositions;
  // Build list from file
  if (params.mutatePositionsFile.compare("")){
    std::ifstream in(params.mutatePositionsFile.c_str());
    std::string tmp;
    while(!in.eof()){
      in >> tmp;
      // Check if the position is not already in the list (remove duplicate indexes)
      if(std::count(mutatePositions.begin(), mutatePositions.end(), atoi(tmp.c_str())) == 0)
	mutatePositions.push_back(atoi(tmp.c_str()));
    }
  }
  else{// Use all positions  
    for (uInt i=0;i<model.N;i++)
      mutatePositions.push_back(i);
  }

  // Assign mutable positions to dom1 or dom2 based on nDomainSplit
  std::vector<uInt> mutatePositionsDom1;
  std::vector<uInt> mutatePositionsDom2;
  for(uInt i=0;i<mutatePositions.size();i++){
    if(mutatePositions[i]<=params.nDomainSplit)
      mutatePositionsDom1.push_back(mutatePositions[i]);
    else
      mutatePositionsDom2.push_back(mutatePositions[i]);
  }
  // Safety check on the number of mutations and domains sizes.
  if(mutatePositions.size()>model.N ||
     mutatePositions.size()<params.nPointMutations ||
     (params.nPointMutationsDom2 &&
      (mutatePositionsDom1.size()<params.nPointMutations || mutatePositionsDom2.size()<params.nPointMutationsDom2))
     ){
    std::cout<<"Inconsistent number of mutation positions"<<std::endl;
    exit(1);
  }
  
  // Main routine: Generate m-point mutants by MC sampling around the native sequence
  std::ofstream out((params.outPrefix+"_orthoEs.dat").c_str(),std::ios::out);
  out<<"#";
  for(unsigned int k=0;k<params.nPointMutations;k++)
    out<<std::left<<std::setw(9)<<"MutD1_"+std::to_string(k+1)<<"\t ";
  
  for(unsigned int k=0;k<params.nPointMutationsDom2;k++)
    out<<std::left<<std::setw(9)<<"MutD2_"+std::to_string(k+1)<<"\t ";
  out<<std::fixed<<std::setprecision(4)<<"dEintra_A*\tdEintra_B*\tdInter_A*B*\tdInter_A*B\tdInterAB*"<<std::endl;
  
  // Compute native energies
  float Eintra1_native,Eintra2_native,Einter_native;
  EVT_computeDomainEnergy(model.P, native.data, model.N, model.q,params.nDomainSplit,
			    Eintra1_native,Eintra2_native,Einter_native);
  
  // Setup initial mutant
  uInt candidateAA;
  uInt* mutant = new uInt[model.N];
  std::memcpy(mutant,&native.data[0],model.N*sizeof(uInt));
  std::vector<uInt> positionsDom1,positionsDom2;
  std::sample(mutatePositionsDom1.begin(),mutatePositionsDom1.end(),std::back_inserter(positionsDom1),
	      params.nPointMutations,rndGenerator);
  for(unsigned int k=0;k<params.nPointMutations;k++){
    do{
      candidateAA=rand()%model.q;
    }while(candidateAA==mutant[positionsDom1[k]]);
    mutant[positionsDom1[k]]=candidateAA;
  }
  std::sample(mutatePositionsDom2.begin(),mutatePositionsDom2.end(),std::back_inserter(positionsDom2),
	      params.nPointMutationsDom2,rndGenerator);
  for(unsigned int k=0;k<params.nPointMutationsDom2;k++){
    do{
      candidateAA=rand()%model.q;
    }while(candidateAA==mutant[positionsDom2[k]]);
    mutant[positionsDom2[k]]=candidateAA;
  }

  float Eintra1,Eintra2,Einter;
  EVT_computeDomainEnergy(model.P, mutant, model.N, model.q,params.nDomainSplit,
			  Eintra1,Eintra2,Einter);

  uInt* AsB = new uInt[model.N];
  uInt* ABs = new uInt[model.N];
  float Einter_AsB,Einter_ABs,Etmp1,Etmp2;

  std::memcpy(ABs,&native.data[0],model.N*sizeof(uInt));
  for(uInt mut2: positionsDom2)
    ABs[mut2]=mutant[mut2];
  EVT_computeDomainEnergy(model.P, ABs, model.N, model.q,params.nDomainSplit,Etmp1,Etmp2,Einter_ABs);
  std::memcpy(AsB,&native.data[0],model.N*sizeof(uInt));
  for(uInt mut1: positionsDom1)
    AsB[mut1]=mutant[mut1];
  EVT_computeDomainEnergy(model.P, AsB, model.N, model.q,params.nDomainSplit,Etmp1,Etmp2,Einter_AsB);

  // Compute M mutants by conditional-MCMC
  uInt iSwitch,posSwitch,posNew,oldAA;
  float Eintra1_new,Eintra2_new,Einter_new;
  float deltaE;
  float domSelector;
  

  float Einter_AsB_new,Einter_ABs_new;
  for (uInt i=0;i<params.nMutants;i++){
    for(uInt move=0;move<model.N*params.sweepsPerSample;move++){
      // Perform a MC step: Select positions and AA to mutate, evaluate energy change, accept by Metropolis.
      domSelector=static_cast<float>(rand())/static_cast<float>(RAND_MAX);
      if(domSelector<0.5){ //Mutate on domain 1
	//Select new site to mutate
	iSwitch=rand()%params.nPointMutations;
	posSwitch=positionsDom1[iSwitch];
	do{
	  posNew=mutatePositionsDom1[rand()%mutatePositionsDom1.size()];
	}while(std::find(positionsDom1.begin(),positionsDom1.end(),posNew) != positionsDom1.end());
      }
      else{ //Mutate on domain 2
	//Select new site to mutate
        iSwitch=rand()%params.nPointMutationsDom2;
        posSwitch=positionsDom2[iSwitch];
        do{
          posNew=mutatePositionsDom2[rand()%mutatePositionsDom2.size()];
        }while(std::find(positionsDom2.begin(),positionsDom2.end(),posNew) != positionsDom2.end());
      }

      //Select new AA
      do{
	candidateAA=rand()%model.q;
      }while(candidateAA == mutant[posNew]);

      //Compute partial dE of canidate mutation
      oldAA=mutant[posSwitch];
      mutant[posSwitch]=native.data[posSwitch];
      mutant[posNew]=candidateAA;
      EVT_computeDomainEnergy(model.P, mutant, model.N, model.q,params.nDomainSplit,
			      Eintra1_new,Eintra2_new,Einter_new);
      
      std::memcpy(ABs,&native.data[0],model.N*sizeof(uInt));
      for(uInt mut2: positionsDom2)
	ABs[mut2]=mutant[mut2];
      std::memcpy(AsB,&native.data[0],model.N*sizeof(uInt));
      for(uInt mut1: positionsDom1)
	AsB[mut1]=mutant[mut1];
      if(posNew<=params.nDomainSplit){
	AsB[posSwitch]=native.data[posSwitch];
	AsB[posNew]=candidateAA;
      }
      else{
	ABs[posSwitch]=native.data[posSwitch];
	ABs[posNew]=candidateAA;
      }
      EVT_computeDomainEnergy(model.P, ABs, model.N, model.q,params.nDomainSplit,Etmp1,Etmp2,Einter_ABs_new);
      EVT_computeDomainEnergy(model.P, AsB, model.N, model.q,params.nDomainSplit,Etmp1,Etmp2,Einter_AsB_new);
            
      //Accept the mutation by conditional Metropolis:
      // dE = (E_new - E_old) - (E(A*,B)_new-E(A*,B)_old) - (E(A,B*)_new - E(A,B*)_old)
      deltaE =((Einter_new - Einter) + (Eintra1_new - Eintra1) + (Eintra2_new - Eintra2) - params.T2*(Einter_AsB_new - Einter_AsB) - params.T2*(Einter_ABs_new - Einter_ABs));
      if (deltaE<0 || std::exp(-deltaE/params.T)>(static_cast<float>(rand())/static_cast<float>(RAND_MAX))){
	Eintra1=Eintra1_new;
	Eintra2=Eintra2_new;
	Einter=Einter_new;
	Einter_AsB=Einter_AsB_new;
	Einter_ABs=Einter_ABs_new;
	if(domSelector<0.5)
	  positionsDom1[iSwitch]=posNew;
	else
	  positionsDom2[iSwitch]=posNew;
      }
      else{
	mutant[posSwitch]=oldAA;
	mutant[posNew]=native.data[posNew];
      }
    }      
    //After nSweeps sweeps, record the current mutant 
    for(uInt mut1: positionsDom1)
      out<<std::left<<std::setw(9)<<std::to_string(native.data[mut1])+
	"_"+std::to_string(mut1)+"_"+std::to_string(mutant[mut1])<<"\t ";
	
    for(uInt mut2: positionsDom2){
      out<<std::left<<std::setw(9)<<std::to_string(native.data[mut2])+
	"_"+std::to_string(mut2)+"_"+std::to_string(mutant[mut2])<<"\t ";
    }
    
    out<<std::fixed<<std::setprecision(4)<<
      Eintra1-Eintra1_native<<"\t"<<Eintra2-Eintra2_native<<"\t"<<Einter-Einter_native<<"\t"<<
      Einter_AsB-Einter_native<<"\t"<<Einter_ABs-Einter_native<<std::endl;
  }
  delete[] mutant;
  delete[] AsB;
  delete[] ABs;
}
