#include "infinity/Economy.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
using namespace infinity;
int main(int argc,char** argv){try{
 Universe u;const auto system=Address{}.child(Kind::Galaxy,0).child(Kind::System,26);const auto network=generateEconomicNetwork(u,system);Economy economy(network);const auto file=std::filesystem::path(argv[2]);
 if(std::string(argv[1])=="write"){
  economy.advanceTo(20);std::ofstream(file)<<economy.serialize();std::ofstream meta(file.string()+".meta");meta<<network->fingerprint<<' '<<economy.moneyTotal()<<' '<<network->sites.size()<<'\n';for(auto value:economy.initialTotal())meta<<value<<' ';meta<<'\n';
  for(const auto& site:network->sites)meta<<site.id.str()<<'\n';std::cout<<"LEGACY fingerprint="<<network->fingerprint<<" sites="<<network->sites.size()<<" money="<<economy.moneyTotal()<<std::endl;
 }else{
  std::ifstream input(file);std::string saved((std::istreambuf_iterator<char>(input)),{});std::ifstream meta(file.string()+".meta");uint64_t fingerprint;int64_t money;size_t count;Stock initial{};meta>>fingerprint>>money>>count;for(auto& value:initial)meta>>value;
#ifdef URBAN_AFTER
  if(network->legacyFingerprint!=fingerprint)throw std::runtime_error("Legacy network fingerprint was not retained");
#endif
  if(count!=network->sites.size())throw std::runtime_error("City expansion changed the economic port set");for(const auto& site:network->sites){std::string id;meta>>id;if(id!=site.id.str())throw std::runtime_error("Economic site identity changed");}
  if(!economy.restore(saved))throw std::runtime_error("Existing real-system economic ledger rejected");
  if(economy.moneyTotal()!=money||economy.initialTotal()!=initial)throw std::runtime_error("Original declared endowment changed");
  economy.advanceTo(24);const auto resave=economy.serialize();Economy restart(network);if(!restart.restore(resave)||restart.initialTotal()!=initial||restart.moneyTotal()!=money)throw std::runtime_error("Second restart lost the endowment epoch");
  std::cout<<"PASS: real system 26 ledger / unchanged "<<count<<" site IDs, money and initial materials / continued simulation and second restart"<<std::endl;
 }
}catch(const std::exception& e){std::cerr<<e.what()<<std::endl;return 1;}}
