#include "Argument.h"
#include "IO.h"
#include "tabix.h"

#include <cassert>
#include <string>
#include <set>
#include <map>
#include <vector>
#include <algorithm>

#include "Utils.h"
#include "VCFUtil.h"

#include "base/SimpleMatrix.h"
#include "base/Kinship.h"
#include "base/Pedigree.h"

#include "third/eigen/Eigen/Core"
#include "third/eigen/Eigen/Eigenvalues"

#include "IO.h"
#include "Regex.h"

class EmpiricalKinship{
 public:
  virtual int addGenotype(const std::vector<double>& g) = 0;
  virtual void calculate() = 0;
  virtual const SimpleMatrix& getKinship() const = 0;
  virtual ~EmpiricalKinship() {};
};

/**
 * IBSKinship matrix and use probability to impute kinship
 * Kinship for marker j
 *          0           1       2       missing
 * 0        2           1       0       2(1-p)
 * 1        1           2       1       1
 * 2        0           1       2       2p
 * missing  2(1-p)      1       2p      2(p^2+q^2)
 */
class IBSKinshipImpute: public EmpiricalKinship {
 public:
  IBSKinshipImpute(): n(0) {};
  // missing genotype is less than 0.0
  int addGenotype(const std::vector<double>& g) {
    if (n == 0) {
      k.resize(g.size(), g.size());
      k.clear();
    }
    ++n;
    geno.resize(g.size());
    double sum = 0.0;
    int nonMiss = 0;
    for (size_t i = 0; i < g.size(); ++i) {
      // check validity
      if (g[i] > 2) {
        return -1;
      }
      if (g[i] < 0) {
        geno[i] = 3;
      } else {
        geno[i] = g[i];
        sum+= g[i];
        ++nonMiss;
      }
    }
    double p = 0.0;
    if (nonMiss > 0) {
      p = sum / nonMiss;
    }
    table[0][0] = table[1][1] = table[2][2] = 2;
    table[0][1] = table[1][0] = table[1][2] = table[2][1] = table[1][3] = table[3][1] = 1;
    table[0][2] = table[2][0] = 0;
    table[0][3] = table[3][0] = 2.0*(1.0 - p);
    table[2][3] = table[3][2] = 2.0*p;
    table[3][3] = 2.0 - 4.0 * p * (1-p);
    for (size_t i = 0; i < geno.size(); ++i) {
      for (size_t j = 0; j <= i; ++j) {
        k[i][j] += table[(int)geno[i]][(int)geno[j]];
      }
    }
    ++n;
    return 0;
  }
  void calculate() {
    if ( n == 0) return;
    for (int i = 0; i < k.ncol(); ++i) {
      for (int j = 0; j <= i; ++j) {
        k[i][j] /= n;
        k[j][i] = k[i][j];
      }
    }
  }
  const SimpleMatrix& getKinship() const{
    return this->k;
  }
  void clear() {
    n = 0;
    k.clear();
  }
 private:
  SimpleMatrix k;
  std::vector<double> geno;
  int n;
  double table[4][4];
}; // IBSKinshipImpute

/**
 * IBSKinship matrix and skip missing genotype
 * Kinship for marker j
 *     0   1   2
 * 0   2   1   0
 * 1   1   2   1
 * 2   0   1   2
 */
class IBSKinship: public EmpiricalKinship {
 public:
  IBSKinship(): n(0) {};
  // missing genotype is less than 0.0
  int addGenotype(const std::vector<double>& g) {
    if (n == 0) {
      k.resize(g.size(), g.size());
      k.clear();
      count.resize(g.size(), g.size());
      count.clear();
    }
    ++n;
    for (size_t i = 0; i < g.size(); ++i) {
      for (size_t j = 0; j <= i; ++j) {
        if (g[i] >= 0 || g[j] >= 0) {
          k[i][j] += 2.0 - abs((int)g[i] - (int)g[j]);
          ++ count[i][j];
        }
      }
    }
    return 0;
  }
  void calculate() {
    if ( n == 0) return;
    for (int i = 0; i < k.ncol(); ++i) {
      for (int j = 0; j <= i; ++j) {
        if (count[i][j] > 0) {
          k[i][j] /= count[i][j];
          k[j][i] = k[i][j];
        }
      }
    }
  }
  const SimpleMatrix& getKinship() const {
    return this->k;
  }
  void clear() {
    n = 0;
    k.clear();
  }
 private:
  SimpleMatrix k;
  SimpleMatrix count;
  int n;
}; // IBSKinship

/**
 * BaldingNicolsKinship matrix
 */
class BaldingNicolsKinship: public EmpiricalKinship {
 public:
  BaldingNicolsKinship(): n(0) {};
  // missing genotype is less than 0.0
  int addGenotype(const std::vector<double>& g) {
    if (n == 0) {
      k.resize(g.size(), g.size());
      k.clear();
    }

    geno.resize(g.size());
    double sum = 0.0;
    // double sumSquare = 0.0;
    int nonMiss = 0;
    for (size_t i = 0; i < g.size(); ++i) {
      // check validity
      if (g[i] > 2) {
        return -1;
      }
      if (g[i] < 0) {
        geno[i] = -9;
      } else {
        geno[i] = g[i];
        sum+= g[i];
        // sumSquare += g[i] * g[i];
        ++nonMiss;
      }
    }
    double mean = 0.0;
    double scale = 0.0;
    // double var = 0.0;
    if (nonMiss > 0) {
      mean = sum / nonMiss;
      // var =  sumSquare / n  - mean * mean;
      scale = sqrt(1.0 / (1.0 - mean/2.0) / mean); // mean = 2*p, var = 2*p*(1-p)
    }

    for (size_t i = 0 ; i < geno.size(); ++i) {
      geno[i] -= mean;
    }
    const size_t numG = g.size();
    #pragma omp for
    for (size_t i = 0; i < numG; ++i) {
      for (size_t j = 0; j <= i; ++j) {
        // as missing genotypes are coded as -9,
        // non missing genotype minus its mean should be larger than -5.
        if (geno[i] >= -5 || geno[j] >= -5) {
          k[i][j] += (geno[i] * geno[j]) * scale;
        }
      }
    }
    // fprintf(stderr, "mean = %g, scale = %g, k[0][0] = %g\n", mean, scale, k[0][0]);

    ++n;
    return 0;
  }
  void calculate() {
    if ( n == 0) return;
    for (int i = 0; i < k.ncol(); ++i) {
      for (int j = 0; j <= i; ++j) {
        k[i][j] /= n;
        k[j][i] = k[i][j];
      }
    }
  }
  const SimpleMatrix& getKinship() const {
    return this->k;
  }
  void clear() {
    n = 0;
    k.clear();
  }
 private:
  SimpleMatrix k;
  std::vector<double> geno;
  int n;
}; // Balding-Nicols matrix



int output( const std::vector<std::string>& famName,
            const std::vector<std::string>& indvName,
            const SimpleMatrix& mat,
            bool performPCA,
            const std::string& outPrefix);

int main(int argc, char** argv){
  time_t currentTime = time(0);
  fprintf(stderr, "Analysis started at: %s", ctime(&currentTime));

  ////////////////////////////////////////////////
  BEGIN_PARAMETER_LIST(pl)
      ADD_PARAMETER_GROUP(pl, "Input/Output")
      ADD_STRING_PARAMETER(pl, inVcf, "--inVcf", "input VCF File")
      // ADD_STRING_PARAMETER(pl, inPed, "--inPed", "input PED File")
      ADD_STRING_PARAMETER(pl, outPrefix, "--out", "output prefix")
      ADD_PARAMETER_GROUP(pl, "Algorithm")
      ADD_STRING_PARAMETER(pl, ped, "--ped", "using PED file.")
      ADD_BOOL_PARAMETER(pl, ibs, "--ibs", "using IBS method.")
      ADD_BOOL_PARAMETER(pl, bn, "--bn", "using Balding-Nicols method.")
      ADD_BOOL_PARAMETER(pl, pca, "--pca", "decomoposite calculated kinship matrix.")      
      ADD_PARAMETER_GROUP(pl, "People Filter")
      ADD_STRING_PARAMETER(pl, peopleIncludeID, "--peopleIncludeID", "give IDs of people that will be included in study")
      ADD_STRING_PARAMETER(pl, peopleIncludeFile, "--peopleIncludeFile", "from given file, set IDs of people that will be included in study")
      ADD_STRING_PARAMETER(pl, peopleExcludeID, "--peopleExcludeID", "give IDs of people that will be included in study")
      ADD_STRING_PARAMETER(pl, peopleExcludeFile, "--peopleExcludeFile", "from given file, set IDs of people that will be included in study")
      ADD_PARAMETER_GROUP(pl, "Range Filter")
      ADD_STRING_PARAMETER(pl, rangeList, "--rangeList", "Specify some ranges to use, please use chr:begin-end format.")
      ADD_STRING_PARAMETER(pl, rangeFile, "--rangeFile", "Specify the file containing ranges, please use chr:begin-end format.")
      ADD_PARAMETER_GROUP(pl, "Site Filter")
      ADD_DOUBLE_PARAMETER(pl, minMAF, "--minMAF", "Specify the minimum MAF threshold to be included in calculating kinship.")
      ADD_DOUBLE_PARAMETER(pl, maxMissing, "--maxMiss", "Specify the maximum allows missing rate to be inclued in calculating kinship.")
      ADD_DOUBLE_PARAMETER(pl, minSiteQual, "--minSiteQual", "Specify minimum site qual")
      ADD_STRING_PARAMETER(pl, annoType, "--anno", "Specify the annotation type to be included in calculating kinship.")
      ADD_PARAMETER_GROUP(pl, "Genotype Filter")
      ADD_DOUBLE_PARAMETER(pl, minGQ, "--minGQ", "Specify the minimum genotype quality, otherwise marked as missing genotype")
      ADD_DOUBLE_PARAMETER(pl, minGD, "--minGD", "Specify the minimum genotype depth, otherwise marked as missing genotype")

      ADD_PARAMETER_GROUP(pl, "Other Function")
      // ADD_BOOL_PARAMETER(pl, variantOnly, "--variantOnly", "Only variant sites from the VCF file will be processed.")
      ADD_STRING_PARAMETER(pl, updateId, "--update-id", "Update VCF sample id using given file (column 1 and 2 are old and new id).")
      END_PARAMETER_LIST(pl)
      ;

  pl.Read(argc, argv);
  pl.Status();

  if (FLAG_REMAIN_ARG.size() > 0){
    fprintf(stderr, "Unparsed arguments: ");
    for (unsigned int i = 0; i < FLAG_REMAIN_ARG.size(); i++){
      fprintf(stderr, " %s", FLAG_REMAIN_ARG[i].c_str());
    }
    fprintf(stderr, "\n");
    abort();
  }

  // REQUIRE_STRING_PARAMETER(FLAG_inVcf, "Please provide input file using: --inVcf");
  if (FLAG_inVcf.empty() && FLAG_ped.empty()) {
    fprintf(stderr, "Please provide input file using: --inVcf or --ped");
    abort();
  }
  REQUIRE_STRING_PARAMETER(FLAG_outPrefix, "Please provide output prefix using: --out");

  if (!FLAG_ped.empty()) {
    fprintf(stderr, "Create kinship from pedigree file.\n");
    
    zhanxw::Pedigree ped;
    zhanxw::Kinship kin;

    if (loadPedigree(FLAG_ped, &ped)) {
      fprintf(stderr, "Failed to load pedigree file [ %s ]!", FLAG_ped.c_str());
      exit(1);
    }
    
    int nPeople = ped.getPeopleNumber();
    // printf("Total %d people loaded: \n", nPeople);
    std::vector<std::string> famName;
    std::vector<std::string> indvName;
    for (int i = 0; i < nPeople; ++i){
      const zhanxw::Person& p = ped.getPeople()[i];
      famName.push_back(ped.getFamilyName(p.getFamily()));
      indvName.push_back(ped.getPersonName(i));
      // printf("%s: ", ped.getPersonName(i));
      // ped.getPeople()[i].dump();
    }
    
    // std::vector<int> seq;
    // if (ped.calculateIterationSequence(&seq)) {
    
    // }
    kin.constructFromPedigree(ped);
    const SimpleMatrix& m = kin.getKinship();

    output(famName, indvName, m, FLAG_pca, FLAG_outPrefix);
    return 0;
  }
  fprintf(stderr, "Create empirical kinship from VCF file.\n");
  if (FLAG_maxMissing == 0.0) {
    fprintf(stderr, "Using default maximum missing rate = 0.05\n");
    FLAG_maxMissing = 0.05;
  }
  if (FLAG_minMAF == 0.0) {
    fprintf(stderr, "Using default minimum MAF = 0.05\n");
    FLAG_minMAF = 0.05;
  }

  const char* fn = FLAG_inVcf.c_str();
  VCFInputFile vin(fn);

  // set range filters here
  // e.g.
  // vin.setRangeList("1:69500-69600");
  vin.setRangeList(FLAG_rangeList.c_str());
  vin.setRangeFile(FLAG_rangeFile.c_str());

  // set people filters here
  if (FLAG_peopleIncludeID.size() || FLAG_peopleIncludeFile.size()) {
    vin.excludeAllPeople();
    vin.includePeople(FLAG_peopleIncludeID.c_str());
    vin.includePeopleFromFile(FLAG_peopleIncludeFile.c_str());
  }
  vin.excludePeople(FLAG_peopleExcludeID.c_str());
  vin.excludePeopleFromFile(FLAG_peopleExcludeFile.c_str());

  // let's write it out.
  if (FLAG_updateId != "") {
    int ret = vin.updateId(FLAG_updateId.c_str());
    fprintf(stdout, "%d samples have updated id.\n", ret);
  }
  Regex regex;
  if (FLAG_annoType.size()) {
    regex.readPattern(FLAG_annoType);
  }

  // set up kinship calculate method
  if ( (int)FLAG_ibs + (int)FLAG_bn != 1) {
    fprintf(stderr, "More than one or none of the empirical kinsip calculation methods specified.\n");
    abort();
  }
  EmpiricalKinship* kinship = NULL;
  if (FLAG_ibs) {
    kinship = new IBSKinship;
  } else if (FLAG_bn) {
    kinship = new BaldingNicolsKinship;
  }
  // get people names
  std::vector<std::string> names; // indvidual sample names
  vin.getVCFHeader()->getPeopleName(&names);
  std::vector<double> genotype;
  genotype.resize(names.size());
  fprintf(stderr, "Total %zu individuals from VCF are used.\n", names.size());
  
  // set threshold
  double maxMissing = 1.0 * FLAG_maxMissing * names.size();
  // double maxAllele = (1.0 - FLAG_minMAF) * 2.0 * names.size();
  // double minAllele = FLAG_minMAF * 2.0 * names.size();

  int lowSiteFreq = 0; // counter of low site qualities
  int filterSite = 0; // counter of site with too many bad genotypes
  int lineNo = 0;
  int skipSexChrom = 0;
  int nonVariantSite = 0;
  int GTidx, GDidx, GQidx;
  bool missing;
  while (vin.readRecord()){
    lineNo ++;
    if (lineNo % 10000 == 0) {
      fprintf(stdout, "\rTotal %d VCF records have processed", lineNo);
    }
    VCFRecord& r = vin.getVCFRecord();
    VCFPeople& people = r.getPeople();
    VCFIndividual* indv;

    // only take autosomal
    int chrom = atoi(chopChr(r.getChrom()));
    if (chrom < 1 && chrom > 22) {
      ++ skipSexChrom;
      continue;
    }
    
    // site filter
    if (FLAG_minSiteQual > 0 && r.getQualDouble() < FLAG_minSiteQual) {
      ++lowSiteFreq;
      continue;
    }
    if (FLAG_annoType.size()) {
      bool isMissing = false;
      const char* tag = r.getInfoTag("ANNO", &isMissing).toStr();
      if (isMissing)
        continue;
      // fprintf(stdout, "ANNO = %s", tag);
      bool match = regex.match(tag);
      // fprintf(stdout, " %s \t", match ? "match": "noMatch");
      // fprintf(stdout, " %s \n", exists ? "exists": "missing");
      if (!match) {
        continue;
      }
    }
    // extract genotype
    bool hasVariant = false;
    int geno;
    GTidx = r.getFormatIndex("GT");
    GDidx = (FLAG_minGD > 0) ? r.getFormatIndex("GD") : -1;
    GQidx = (FLAG_minGQ > 0) ? r.getFormatIndex("GQ") : -1;

    int ac = 0;
    double af = 0;
    int nMiss = 0;
    // extract data
    for (size_t i = 0; i < people.size() ;i ++) {
      indv = people[i];
      geno = indv->get(GTidx, &missing).getGenotype(); // here missing mean if GT exists
      if (!missing && geno >= 0 && GDidx >= 0) {
        int gd = indv->get(GDidx, &missing).toInt();
        if (gd < FLAG_minGD) {
          missing = true;
        }
      }
      if (!missing && geno >= 0 && GQidx >= 0) {
        int gq = indv->get(GQidx, &missing).toInt();
        if (gq < FLAG_minGQ) {
          missing = true;
        }
      }
      // set genotype
      if (missing || geno < 0) {
        genotype[i] = -9;
        ++ nMiss;
      } else {
        genotype[i] = geno;
        ac += geno;
        if (geno != 0)
          hasVariant = true;
      }
    }
    if (nMiss >  maxMissing) {
      filterSite ++;
      continue;
    }

    af = 0.5 * ac / (genotype.size() - nMiss);
    if (af < FLAG_minMAF || af > 1.0 - FLAG_minMAF) {
      filterSite ++;
      continue;
    }
    if (!hasVariant) {
      nonVariantSite++;
      continue;
    }
    kinship->addGenotype(genotype);
  }

  // output
  kinship->calculate();
  const SimpleMatrix& ret = kinship->getKinship();

  output(names, names, ret, FLAG_pca, FLAG_outPrefix);
  if (!kinship)
    delete kinship;
  
  // end
  fprintf(stdout, "Total %d VCF records have converted successfully\n", lineNo);
  if (skipSexChrom) {
    fprintf(stdout, "Skipped %d variants non autosomal variants\n", skipSexChrom);    
  }
  if (nonVariantSite) {
    fprintf(stdout, "Skipped %d non-variant VCF records\n", nonVariantSite);
  }
  if (lowSiteFreq) {
    fprintf(stdout, "Skipped %d low sites due to site quality lower than %f\n", lowSiteFreq, FLAG_minSiteQual);
  };
  if (filterSite) {
    fprintf(stdout, "Skipped %d sites due to MAF or high misssingness\n", filterSite);
  }
  fprintf(stdout, "Total %d variants are used to calculate kinship matrix.\n", lineNo - nonVariantSite - lowSiteFreq - filterSite);
  return 0;
};

int output( const std::vector<std::string>& famName,
            const std::vector<std::string>& indvName,
            const SimpleMatrix& mat,
            bool performPCA,
            const std::string& outPrefix) {
  if (famName.size() != indvName.size()) {
    return -1;
  }

  if (mat.nrow() != mat.ncol() || mat.nrow() != (int)indvName.size()) {
    return -1;
  }
  
  FILE* out = fopen( (outPrefix + ".kinship").c_str(), "w");
  // write heade
  fprintf(out, "FID\tIID");
  for (size_t i = 0; i < famName.size(); ++i) {
    fprintf(out, "\t%s", indvName[i].c_str());
  }
  fprintf(out, "\n");
  // write content
  for (int i = 0; i < mat.ncol(); ++i) {
    fprintf(out, "%s\t%s", famName[i].c_str(), indvName[i].c_str());
    for (int j = 0; j < mat.ncol(); ++j) {
      fprintf(out, "\t%g", mat[i][j]);
    }
    fprintf(out, "\n");
  }
  fclose(out);

  if (performPCA) {
    Eigen::MatrixXf m;
    m.resize(famName.size(), famName.size());
    for (size_t i = 0; i < famName.size(); ++i){
      for (size_t j = 0; j < famName.size(); ++j){
        m(i, j) = mat[i][j];
      }
    }
    
    // delete kinship; // release memory
    // kinship = NULL;
    
    Eigen::SelfAdjointEigenSolver<Eigen::MatrixXf> es(m);
    if (es.info() == Eigen::Success) {
      const Eigen::MatrixXf& U = es.eigenvectors();
      const Eigen::MatrixXf& V = es.eigenvalues();

      // output
      FILE* out = fopen( (outPrefix + ".pca").c_str(), "w");
      fprintf(out, "FID\tIID");
      fprintf(out, "\tLambda");
      for (size_t i = 0; i < famName.size(); ++i) {
        fprintf(out, "\tU%zu", (i + 1));
      }
      fprintf(out, "\n");
      // output eigenvalue from the biggest to smallest
      for (size_t i = 0; i < famName.size(); ++i) {
        fprintf(out, "%s\t%s", famName[i].c_str(), indvName[i].c_str());
        fprintf(out, "\t%g", V(famName.size() - 1 - i));
        for (size_t j = 0; j < famName.size(); ++j) {
          fprintf(out, "\t%g", U(famName.size() - 1 - i, j));
        }
        fprintf(out, "\n");
      }
      fclose(out);
    } else{
      fprintf(stderr, "Kinship decomposition failed!\n");
    }
  }
  return 0;
}
