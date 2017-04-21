#include "chase_blas.hpp"
#include "testresult.hpp"
//#include "../include/lanczos.h"


#include <random>
#include <boost/program_options.hpp>

namespace po = boost::program_options;

template<typename T>
void readMatrix( T *H, std::string path_in, std::string spin, std::size_t kpoint, std::size_t index,
                 std::string suffix, std::size_t size, bool legacy )
{
  std::ostringstream problem(std::ostringstream::ate);
  if(legacy)
    problem << path_in << "gmat  1 " << std::setw(2) << index << suffix;
  else
    problem << path_in << "mat_" << spin << "_" << std::setfill('0') << std::setw(2)
            << kpoint << "_" << std::setfill('0') << std::setw(2) << index << suffix;

  std::cout << problem.str() << std::endl;
  std::ifstream input( problem.str().c_str(), std::ios::binary );
  if(input.is_open()) {
    input.read((char *) H, sizeof(T) * size);
  } else {
    throw std::string("error reading file: ") + problem.str();
  }
}

typedef std::complex< double > T;

int main(int argc, char* argv[])
{

  std::size_t N;
  std::size_t nev;
  std::size_t nex;
  std::size_t deg;
  std::size_t bgn;
  std::size_t end;

  double tol;
  bool sequence;

  std::string path_in;
  std::string mode;
  std::string opt;
  std::string arch;
  std::string path_eigp;
  std::string path_out;
  std::string path_name;

  std::size_t kpoint;
  bool legacy;
  std::string spin;

  po::options_description desc("ChASE Options");
  desc.add_options()
    ("help,h", "show this message")
    ("n", po::value<std::size_t>(&N)->required(), "Size of the Input Matrix")
    ("nev", po::value<std::size_t>(&nev)->required(), "Wanted Number of Eigenpairs")
    ("nex", po::value<std::size_t>(&nex)->default_value(25), "Extra Search Dimensions")
    ("deg", po::value<std::size_t>(&deg)->default_value(20), "Initial filtering degree")
    ("bgn", po::value<std::size_t>(&bgn)->default_value(2), "Start ell")
    ("end", po::value<std::size_t>(&end)->default_value(2), "End ell")
    ("spin", po::value<std::string>(&spin)->default_value("d"), "spin")
    ("kpoint", po::value<std::size_t>(&kpoint)->default_value(0), "kpoint")
    ("tol", po::value<double>(&tol)->default_value(1e-10), "Tolerance for Eigenpair convergence")
    ("path_in", po::value<std::string>(&path_in)->required(), "Path to the input matrix/matrices")
    ("mode", po::value<std::string>(&mode)->default_value("A"), "valid values are R(andom) or A(pproximate)")
    ("opt", po::value<std::string>(&opt)->default_value("S"), "Optimi(S)e degree, or do (N)ot optimise")
    ("path_eigp", po::value<std::string>(&path_eigp), "Path to approximate solutions, only required when mode is Approximate, otherwise not used")
    ("sequence", po::value<bool>(&sequence)->default_value(false), "Treat as sequence of Problems. Previous ChASE solution is used, when available")
    ("legacy", po::value<bool>(&legacy)->default_value(false), "Use legacy naming scheme?")
    ;

  std::string testName;
  po::options_description testOP("Test options");
  testOP.add_options()
    ("write", "Write Profile")
    ("name", po::value<std::string>(&testName)->required(), "Name of the testing profile")
    ;

  desc.add(testOP);

  po::variables_map vm;
  po::store(po::parse_command_line(argc, argv, desc), vm);

  if (vm.count("help")) {
    std::cout
      << desc << std::endl;
    return 1;
  }

  try {
    po::notify(vm);
  }
  catch( std::exception &e )
  {
    std::cout
      << e.what() << std::endl
      << std::endl
      << desc << std::endl;
    return -1;
  }

  mode = toupper(mode.at(0));
  opt = toupper(opt.at(0));

  if( bgn > end )
  {
    std::cout << "Begin must be smaller than End!" << std::endl;
    return -1;
  }

  if( mode != "R" && mode != "A" )
  {
    std::cout
      << "Illegal value for mode: \"" << mode << "\"" << std::endl
      << "Legal values are R or A" << std::endl;
    return -1;
  }

  if( opt != "N" && opt != "S" && opt != "M" )
  {
    std::cout
      << "Illegal value for opt: " << opt << std::endl
      << "Legal values are N, S, M" << std::endl;
    return -1;
  }

  if( path_eigp.empty() && mode == "A" )
  {
    std::cout << "eigp is required when mode is " << mode << std::endl;
    // TODO verify that eigp is a valid path
    return -1;
  }

  // -----Validation of test----
  bool testResultCompare;
  if( vm.count("write") )
    testResultCompare = false;
  else
    testResultCompare = true;

  TestResult TR(
    testResultCompare,
    testName,
    N,
    nev,
    nex,
    deg,
    tol,
    mode[0],
    opt[0],
    sequence
    );

  const std::size_t nevex = nev + nex; // Block size for the algorithm.
  Base< T > * Lambda = new Base< T >[nevex];

  ChASE_Config config;
  config.setTol( tol );
  config.setDeg( deg );

  ChASE_Blas< T > *single
    = new ChASE_Blas< T >( N, nev, nex, Lambda, config );

  MKL_Complex16 *_H = new MKL_Complex16[N*N];

  /*
  // Matrix representing the generalized eigenvalue problem.
  
  // Matrix which stores the approximate eigenvectors
  MKL_Complex16 *V = new MKL_Complex16[N*nevex];
  // Matrix which stores the eigenvectors
  MKL_Complex16 *W = new MKL_Complex16[N*nevex];
  // eigenvalues
  std::size_t *degrees  = new std::size_t[nevex];
  */

  //----------------------------------------------------------------------------
  //std::random_device rd;
  std::mt19937 gen(2342.0);
  std::normal_distribution<> d;


  T *V = single->getVectorsPtr();
  T *H = single->getMatrixPtr();
  T *W = new T[N*nevex];


  for (auto i = bgn; i <= end ; ++i)
  {
    if( i == bgn || !sequence )
    {
      /*
	if (path_eigp == "_" && int_mode == OMP_APPROX && i == bgn )
	{ // APPROX. No approximate pairs given.
	  //-------------------------SOLVE-PREVIOUS-PROBLEM-------------------------
	  app = ".bin"; // Read the matrix of the previous problem.
	  myreadwrite<MKL_Complex16>(H, path_in.c_str(), app.c_str(), i-1, N*N, 'r');

	  // Solve the previous problem, store the eigenpairs in V and Lambda.
	  ZHEEVR("V", "I", "L", &N, H, &N, &vl, &vu, &il, &iu, &tol,
	  &notneeded, Lambda, V, &N, isuppz, zmem, &lzmem, dmem, &ldmem,
	  imem, &limem, &INFO);

	  //------------------------------------------------------------------------
	  // In next iteration the solutions to this one will be used as approximations.
	  path_eigp = path_out;
	  }
	  else */
      if (mode[0] == 'A')
      { // APPROX. Approximate eigenpairs given.
        //-----------------------READ-APPROXIMATE-EIGENPAIRS----------------------
        readMatrix( V, path_eigp, spin, kpoint, i-1, ".vct", N*nevex, legacy );
        readMatrix( Lambda, path_eigp, spin, kpoint, i-1, ".vls", nevex, legacy );
        //------------------------------------------------------------------------
      }
      else
      { // RANDOM.
        // Randomize V.
        for( std::size_t i=0; i < N*nevex; ++i)
        {
          V[i] = T( d(gen), d(gen) );
        }
        // Set Lambda to zeros. ( Lambda = zeros(N,1) )
        for(int j=0; j<nevex; j++)
          Lambda[j]=0.0;
      }
    }
    else
    {
      // when doing a sequence and we are not in the first iteration, we use the
      // previous solution
      mode = "A";
    }
    readMatrix( _H, path_in, spin, kpoint, i, ".bin", N*N, legacy);
    // the input is complex double so we cast to T
    for( std::size_t idx = 0; idx < N*N; ++idx )
      H[idx] = _H[idx];
    single->shift(1);



    Base< T > normH = std::max(t_lange('1', N, N, H, N), Base< T >(1.0) );
    single->setNorm( normH );


    /*
    // test lanczos
    {
      double upperb;
      double *ritzv_;
      MKL_Complex16 *V_;
      std::size_t num_its = 10;
      std::size_t numvecs = 4;
      if( mode[0] == CHASE_MODE_RANDOM )
      {
        ritzv_ = new double[nevex];
        num_its = std::min((std::size_t)40,nevex/numvecs);
        num_its = std::max((std::size_t)1,num_its);
        V_ = new MKL_Complex16[num_its*N];
      }
      lanczos( H, N, numvecs, num_its, nevex, &upperb,
               mode[0] == CHASE_MODE_RANDOM,
               ritzv_, V_);
      if( mode[0] == CHASE_MODE_RANDOM )
      {
        double lambda = * std::min_element( ritzv_, ritzv_ + nevex );
        double lowerb = * std::max_element( ritzv_, ritzv_ + nevex );
        TR.registerValue( i, "lambda1", lambda );
        TR.registerValue( i, "lowerb", lowerb );
        delete[] ritzv_;
        delete[] V_;
      }
      TR.registerValue( i, "upperb", upperb );
    }
    */
    //------------------------------SOLVE-CURRENT-PROBLEM-----------------------
    //chase( single, N, Lambda, nev, nex, deg );
    single->solve();
    //--------------------------------------------------------------------------

    ChASE_PerfData perf = single->getPerfData();

    std::size_t iterations = perf.get_iter_count();
    std::size_t filteredVecs = perf.get_filtered_vecs();

    TR.registerValue( i, "filteredVecs", filteredVecs );
    TR.registerValue( i, "iterations", iterations );

    //-------------------------- Calculate Residuals ---------------------------
    T *V = single->getVectorsPtr();
    for( CHASE_INT j = 0; j < N*(nev+nex); ++j )
      {
        W[j] = V[j];
      }

    //    memcpy(W, V, sizeof(MKL_Complex16)*N*nev);
    T one(1.0);
    T zero(0.0);
    T eigval;
    int iOne = 1;
    for(int ttz = 0; ttz<nev;ttz++){
      eigval = -1.0 * Lambda[ttz];
      t_scal( N, &eigval, W+ttz*N, 1);
    }
    t_hemm(
      CblasColMajor,
      CblasLeft,
      CblasLower,
      N, nev, &one, H, N, V, N, &one, W, N);
    Base<T> norm = t_lange( 'M', N, nev, W, N);
    TR.registerValue( i, "resd", norm);


    // Check eigenvector orthogonality
    T *unity = new T[nev*nev];
    T neg_one(-1.0);
    for(int ttz = 0; ttz < nev; ttz++){
      for(int tty = 0; tty < nev; tty++){
        if(ttz == tty) unity[nev*ttz+tty] = 1.0;
        else unity[nev*ttz+tty] = 0.0;
      }
    }

    t_gemm(
      CblasColMajor,
      CblasConjTrans, CblasNoTrans,
      nev, nev, N,
      &one,
      V, N,
      V, N,
      &neg_one,
      unity, nev
      );

    Base<T> norm2 = t_lange( 'M', nev, nev, unity, nev);
    TR.registerValue( i, "orth", norm2);

    delete[] unity;

    std::cout << "resd: " << norm << "\torth:" << norm2 << std::endl;

    perf.print_timings();
    std::cout << "Filtered Vectors\t\t" << filteredVecs << std::endl;

  } // for(int i = bgn; i <= end; ++i)


  TR.done();

#ifdef PRINT_EIGENVALUES
  std::cout << "Eigenvalues: " << std::endl;
  for(int zzt = 0; zzt < nev; zzt++)
    std::cout << std::setprecision(16) << Lambda[zzt] << std::endl;
  std::cout << "End of eigenvalues. " << std::endl;
#endif


  //delete[] H;
  //delete[] V;
  delete[] W;
  delete[] Lambda;
  //delete[] zmem; delete[] dmem; delete[] imem;
  //delete[] isuppz;

  return 0;
}
