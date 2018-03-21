/*
INPUT:

GCM grid spec  (string: grid name)

Ice (hi-res) grid spec
FGICE, elev --> elevmaskI
    --> AbbrGrid, only of ice-covered grid cells

elevations for ECs


OUTPUT:

unscaled matrices IvA, IvE, AvE


Run with:
    ulimit -v 8000000

Regular earth has 
   nice=26064734   ice-covered gridcells
*/


#include <string>
#include <sstream>
#include <iostream>

#include <boost/filesystem.hpp>
#include <boost/algorithm/string.hpp>

#include <tclap/CmdLine.h>

#include <prettyprint.hpp>
#include <ibmisc/netcdf.hpp>
#include <ibmisc/stdio.hpp>
#include <ibmisc/filesystem.hpp>
#include <spsparse/SparseSet.hpp>

//#include <icebin/Grid.hpp>
//#include <icebin/AbbrGrid.hpp>
#include <icebin/GCMRegridder.hpp>
#include <icebin/modele/GCMRegridder_ModelE.hpp>
#include <icebin/modele/grids.hpp>
#include <icebin/modele/hntr.hpp>

#include <icebin/gridgen/GridGen_LonLat.hpp>

using namespace std;
using namespace ibmisc;
using namespace icebin;
using namespace icebin::modele;
using namespace netCDF;
using namespace spsparse;

static double const NaN = std::numeric_limits<double>::quiet_NaN();

// This parameter controls memory use.  Larger = more memory, smaller = more segments
static int const chunk_size = 4000000;    // Not a hard limit


// ==========================================================
struct ParseArgs {
    HntrSpec hspecO;    // Name of Hntr Spec for Ocean grid
    HntrSpec hspecI;    // Name of Hntr Spec for Ice Grid
    HntrSpec hspecI2;

    std::string nc_fname;
    std::string fgiceI_vname;
    std::string elevI_vname;

//    std::string nc foceanI_fname;
    std::string topoO_fname;
//    std::string foceanI_vname;
//    std::string 

    std::string ofname;
    std::array<double,2> ec_range;    // Lowest and highest elevation classes [m]
    double ec_skip;                    // Distance between elevation classes [m]
    bool scale;
    bool const correctA = false;    // Only needed with projected I grids (and then not really)
    std::array<double,3> sigma;    // NOTE: Smoothing in general does not work when ice is sectioned.  Should be applied later if user wants it.

    double eq_rad;        // Radius of earth; see ModelE code    

    bool run_chunk;        // true if we should compute ice for a chunk; false if we should compute the chunk boundaries
    int chunk_no=-1;
    std::array<std::array<int,2>,2> chunk_range;    // {{x0,y0},{x1,y1}}

    ParseArgs(int argc, char **argv);
};


ostream& operator<<(ostream& os, ParseArgs const &args)
{  
    return os << "ParseArgs(" << endl
        << "    hspecO: " << args.hspecO.im << "x" << args.hspecO.jm << endl
        << "    hspecI: " << args.hspecI.im << "x" << args.hspecI.jm << endl
        << "    nc_fname: " << args.nc_fname << " -- " << args.fgiceI_vname << " -- " << args.elevI_vname << endl
        << "    topoO_fname: " << args.topoO_fname << endl
        << "    ofname: " << args.ofname << endl
        << "    ec_range: " << args.ec_range << "  ec_skip=" << args.ec_skip << endl
        << "    scale: " << (args.scale ? "true" : "false") << endl
        << "    sigma: " << args.sigma;
}  

template<class DestT>
static std::vector<DestT> parse_csv(std::string scsv_str)
{
    // Parse to vector of strings
    std::vector<std::string> scsv;
    boost::algorithm::split(scsv,  scsv_str, boost::is_any_of(","));


    std::vector<DestT> ret;
    for (std::string &s : scsv) {
        stringstream myString(s);
        DestT val;
        myString >> val;
        ret.push_back(val);
    }


    return ret;
}

ParseArgs::ParseArgs(int argc, char **argv)
    : sigma(std::array<double,3>{0,0,0})
{

    // Wrap everything in a try block.  Do this every time, 
    // because exceptions will be thrown for problems.
    try {  
        TmpAlloc tmp;

        // Define the command line object, and insert a message
        // that describes the program. The "Command description message" 
        // is printed last in the help text. The second argument is the 
        // delimiter (usually space) and the last one is the version number. 
        // The CmdLine object parses the argv array based on the Arg objects
        // that it contains. 
        TCLAP::CmdLine cmd("Command description message", ' ', "<no-version>");

        TCLAP::UnlabeledValueArg<std::string> shspecO_a(
            "gridO", "Name of Ocean grid (eg: g1qx1)",
            true, "g1qx1", "atm. grid name", cmd);

        TCLAP::UnlabeledValueArg<std::string> shspecI_a(
            "gridI", "Name of Ice grid (eg: g1mx1m)",
            true, "g1mx1m", "ice grid name", cmd);

        TCLAP::UnlabeledValueArg<std::string> shspecI2_a(
            "gridI2", "Name of Display Ice grid (eg: ghxh)",
            true, "ghxh", "display ice grid name", cmd);

        TCLAP::UnlabeledValueArg<std::string> nc_fname_a(
            "elevmaskI-fname",
                "NetCDF file containing ice mask and elevation (1 where there is ice)",
            true, "etopo1_ice_g1m.nc", "mask filename", cmd);

        TCLAP::UnlabeledValueArg<std::string> topoO_fname_a(
            "topoO-fname",
                "ModelE TOPO file on the Ocean grid.  Need FOCEAN and FOCEANF",
            true, "topoo.nc", "focean filename", cmd);





        TCLAP::ValueArg<std::string> foceanI_vname_a("n", "focean",
            "Name of NetCDF variable containing ice focean (1 where there is ice)",
            false, "FGICE1m", "focean var name", cmd);

        TCLAP::ValueArg<std::string> fgiceI_vname_a("m", "mask",
            "Name of NetCDF variable containing ice mask (1 where there is ice)",
            false, "FGICE1m", "mask var name", cmd);

        TCLAP::ValueArg<std::string> elevI_vname_a("e", "elev",
            "Name of NetCDF variable containing elevation [m]",
            false, "ZICETOP1m", "elevation var name", cmd);

        TCLAP::ValueArg<std::string> ec_a("E", "elev-classes",
            "Elevations [m] for the elevation classes: lowest,highest,skip",
            false, "-100,3700,200", "elevations", cmd);

        TCLAP::ValueArg<std::string> ofname_a("o", "output",
            "Output filename (NetCDF) for ECs",
            false, "global_ec.nc", "mask var name", cmd);

        TCLAP::SwitchArg raw_a("r", "raw",
             "Produce raw (unscaled) matrices?",
             cmd, false);

// Smoothing does not work with sectioned ice
//        TCLAP::ValueArg<std::string> sigma_a("g", "sigma",
//            "Sommthing distances: x,y,z",
//            false, "0,0,0", "smoothing distances", cmd);

        TCLAP::ValueArg<double> eq_rad_a("R", "radius",
            "Radius of the earth",
            false, modele::EQ_RAD, "earth radius", cmd);

        TCLAP::ValueArg<std::string> runchunk_a("c", "runchunk",
            "Runs on ice over a segmenet of fgiceO (not for end-user use)",
            false, "", "O cell range", cmd);


        // Not needed for spherical grids
        // TCLAP::SwitchArg correctA_a("c", "correct",
        //      "Correct for area changes due to projection?",
        //      cmd, false);

        // Parse the argv array.
        cmd.parse( argc, argv );

        // Get the value parsed by each arg.
        hspecO = *modele::grids.at(shspecO_a.getValue());
        hspecI = *modele::grids.at(shspecI_a.getValue());
        hspecI2 = *modele::grids.at(shspecI2_a.getValue());
        nc_fname = nc_fname_a.getValue();
        fgiceI_vname = fgiceI_vname_a.getValue();
        elevI_vname = elevI_vname_a.getValue();
        topoO_fname = topoO_fname_a.getValue();

        // Parse elevation classes...
        auto _ec(parse_csv<double>(ec_a.getValue()));
        if (_ec.size() < 2 || _ec.size() > 3) (*icebin_error)(-1,
            "--ec '%%s' must have just two or three values", ec_a.getValue().c_str());
        ec_range[0] = _ec[0];
        ec_range[1] = _ec[1];
        ec_skip = (_ec.size() == 3 ? _ec[2] : 1);

        ofname = ofname_a.getValue();
        scale = !raw_a.getValue();

        eq_rad = eq_rad_a.getValue();

        std::string srunchunk(runchunk_a.getValue());
        if (srunchunk == "") {
            run_chunk = false;
        } else {
            auto bounds(parse_csv<double>(srunchunk));
            if (bounds.size() != 5) (*icebin_error)(-1,
                "--runchunk '%s' must have 5 values", srunchunk.c_str());

            run_chunk = true;
            chunk_no = bounds[0];
            chunk_range[0][0] = bounds[1];
            chunk_range[0][1] = bounds[2];
            chunk_range[1][0] = bounds[3];
            chunk_range[1][1] = bounds[4];
    }

#if 0
        // Parse sigma
        auto _sigma(parse_csv<double>(sigma_a.getValue()));
        if (sigma.size() != 3) (*icebin_error)(-1,
            "sigma must have exactly three elements");
        for (int i=0; i<_sigma.size(); ++i) sigma[i] = _sigma[i];
#endif

    } catch (TCLAP::ArgException &e) { // catch any exceptions
        std::cerr << "error: " << e.error() << " for arg " << e.argId() << std::endl;
        exit(1);
    }
}

// ==========================================================

class ExchAccum {
    ExchangeGrid &exgrid;
    blitz::Array<double,1> const &elevmaskI;
    SparseSet<long,int> &dimO;        // Dimension is created but not used
    SparseSet<long,int> &dimI;        // Dimension is created but not used
public:
    ExchAccum(
        ExchangeGrid &_exgrid,
        blitz::Array<double,1> const &_elevmaskI,
        SparseSet<long,int> &_dimO,
        SparseSet<long,int> &_dimI)
    : exgrid(_exgrid), elevmaskI(_elevmaskI), dimO(_dimO), dimI(_dimI) {}

    void add(std::array<int,2> const &index, double area)
    {
        auto const iO(index[0]);
        auto const iI(index[1]);
        if (!std::isnan(elevmaskI(iI))) {
            // Save as sparse indexing, as required by IceRegridder::init()
            exgrid.add(index, area);
int sz = exgrid.dense_extent();
if (sz % 100000 == 0) printf("exgrid size=%d\n", sz);
            dimO.add_dense(iO);
            dimI.add_dense(iI);
        }
    }
};

void nocompress_configure_var(netCDF::NcVar ncvar)
{
    ncvar.setCompression(true, true, 4);

    // For some reason, this causes an HDF5 error
    // ncvar.setChecksum(netCDF::NcVar::nc_FLETCHER32);
}


linear::Weighted_Eigen make_I2vX(
    linear::Weighted_Eigen const &IvX,
    ParseArgs const &args,
    SparseSet<long,int> &dimI2,
    SparseSet<long,int> &dimI,
    SparseSet<long,int> &dimX,
    RegridParams const &params)
{

    // I2vI: Convert to plottable global ice grid
    Hntr hntr_IvI2(17.17, args.hspecI, args.hspecI2);
    EigenSparseMatrixT I2vI(MakeDenseEigenT(
        std::bind(&Hntr::overlap<MakeDenseEigenT::AccumT,DimClip>,
            &hntr_IvI2, std::placeholders::_1, args.eq_rad, DimClip(&dimI)),
        {SparsifyTransform::TO_DENSE_IGNORE_MISSING, SparsifyTransform::ADD_DENSE},
        {&dimI, &dimI2}, 'T').to_eigen());

    auto sI2vI(sum(I2vI,0,'-'));
    auto I2vIs(sum(I2vI,1,'-'));
    linear::Weighted_Eigen I2vX(
        std::array<SparseSet<long, int>*, 2>{&dimI2, &dimX},
        IvX.conservative);
    auto &wI2vX_e(I2vX.tmp.make<EigenColVectorT>(
        I2vI * map_eigen_diagonal(I2vIs) * map_eigen_colvector(IvX.wM)));
    I2vX.wM.reference(to_blitz(wI2vX_e));
    I2vX.Mw.reference(IvX.Mw.copy());
    if (params.scale) {
        I2vX.M.reset(new EigenSparseMatrixT(
            map_eigen_diagonal(sI2vI) * I2vI * *IvX.M));
    } else {
        blitz::Array<double,1> sIvX(1. / IvX.wM);
        I2vX.M.reset(new EigenSparseMatrixT(
            I2vI * map_eigen_diagonal(sIvX) * *IvX.M));
    }

    return I2vX;
}


std::unique_ptr<GCMRegridder> new_gcmA_standard(
    HntrSpec &hspecA, std::string const &grid_name,
    ParseArgs const &args, blitz::Array<double,2> const &elevmaskI)
{
    ExchangeGrid aexgrid;    // Put our answer in here

    auto const &hspecI(args.hspecI);
    modele::Hntr hntr(17.17, hspecA, hspecI);


    // -------------------------------------------------------------
    printf("---- Computing overlaps\n");

    // Compute overlaps for cells with ice
    SparseSet<long,int> _dimA;    // Only include A grid cells with ice
    SparseSet<long,int> _dimI;    // Only include I grid cells with ice
    hntr.overlap(ExchAccum(aexgrid, reshape1(elevmaskI), _dimA, _dimI), args.eq_rad);

    // -------------------------------------------------------------
    printf("---- Creating gcmA for %s\n", grid_name.c_str());

    // Turn HntrSpec --> GridSpec
    GridSpec_LonLat specA(make_grid_spec(hspecA, false, 1, args.eq_rad));
    GridSpec_LonLat specI(make_grid_spec(hspecI, false, 1, args.eq_rad));

    // Realize A grid for relevant gridcells
    auto agridA(make_abbr_grid(grid_name, specA, std::move(_dimA)));

    // Set up elevation classes    
    std::vector<double> hcdefs;
    for (double elev=args.ec_range[0]; elev <= args.ec_range[1]; elev += args.ec_range[2]) {
        hcdefs.push_back(elev);
    }

    // Create standard GCMRegridder for A <--> I
    std::unique_ptr<GCMRegridder_Standard> gcmA(new GCMRegridder_Standard);
    gcmA->init(
        std::move(agridA), std::move(hcdefs),
        Indexing({"A", "HC"}, {0,0}, {agridA.dim.sparse_extent(), hcdefs.size()}, {1,0}),
        args.correctA);


    // --------------------------------------------------
    // Create IceRegridder for I and add to gcmA
    auto ice(new_ice_regridder(IceRegridder::Type::L0));
    auto agridI(make_abbr_grid("Ice", specI, std::move(_dimI)));
    ice->init("globalI", gcmA->agridA, nullptr,
        std::move(agridI), std::move(aexgrid),
        InterpStyle::Z_INTERP);    // You can use different InterpStyle if you like.

    gcmA->add_sheet(std::move(ice));

    return std::unique_ptr<GCMRegridder>(gcmA.release());
}

std::unique_ptr<GCMRegridder> new_gcmA_mismatched(
    FileLocator const &files, ParseArgs const &args, blitz::Array<double,2> const &elevmaskI)
{
    auto const &hspecO(args.hspecO);
    auto const &hspecI(args.hspecI);

    auto gcmO(new_gcmA_standard(hspecO, "Ocean", args, elevmaskI));


    // --------------------------------------------------
    printf("---- Creating gcmA\n");

    // Create a mismatched regridder, to mediate between different ice
    // extent of GCM vs. IceBin
    std::unique_ptr<modele::GCMRegridder_ModelE> gcmA(
        new modele::GCMRegridder_ModelE(
            std::shared_ptr<GCMRegridder>(gcmO.release())));

    HntrSpec const &hspecA(cast_GridSpec_LonLat(*gcmA.agridA.spec).hntr);

    // Load the fractional ocean mask (based purely on ice extent)
    {auto fname(files.locate(args.topoO_fname));

        blitz::Array<double,2> foceanO(hspecO.jm, hspecO.im);    // called FOCEAN in make_topoo
        blitz::Array<double,2> foceanfO(hspecO.jm, hspecO.im);    // called FOCEANF in make_topoo

        printf("---- Reading FOCEAN: %s\n", fname.c_str());
        NcIO ncio(fname, 'r');
        ncio_blitz(ncio, foceanO, "FOCEAN", "double", {});
        ncio_blitz(ncio, foceanfO, "FOCEANF", "double", {});


        gcmA->foceanAOp = reshape1(foceanfO);  // COPY
        gcmA->foceanAOm = reshape1(foceanO);   // COPY
    }

    return std::unique_ptr<GCMRegridder>(gcmA.release());
}



void global_ec_section(GCMRegridder const &gcmA, std::string const &runtype, ParseArgs const &args, blitz::Array<double,2> const &elevmaskI)
{

    std::unique_ptr<RegridMatrices_Dynamic> rm(gcmA->regrid_matrices(0, reshape1(elevmaskI)));

    // ---------- Generate and store the matrices
    // Use the mismatched regridder to create desired matrices and save to file
    RegridParams params(args.scale, args.correctA, args.sigma);
    SparseSet<long,int> dimA, dimI, dimE;
    SparseSet<long,int> dimI2;

    auto nocompress(
            std::bind(nocompress_configure_var, std::placeholders::_1));


    std::string ofname(strprintf("%s-%s-%02d", args.ofname.c_str(), runtype.c_str(), args.chunk_no));

    {NcIO ncio(ofname, 'w', nocompress);
        printf("---- Saving metadat\n");

        HntrSpec const &hspecA(cast_GridSpec_LonLat(
            *gcmA.agridA.spec).hntr);
        HntrSpec const &hspecI(cast_GridSpec_LonLat(
            *gcmA.ice_regridders()[0]->agridI.spec).hntr);

        hspecA.ncio(ncio, "hspecA");
        hspecI.ncio(ncio, "hspecI");

        gcmA.ice_regridders()[0]->agridI.indexing.ncio(ncio, "indexingI");
        gcmA.indexing(GridAE::A).ncio(ncio, "indexingA");
        gcmA.indexingHC.ncio(ncio, "indexingHC");
        gcmA.indexing(GridAE::E).ncio(ncio, "indexingE");




        printf("---- Generating AvI\n");
        auto mat(rm->matrix_d("AvI", {&dimA, &dimI}, params));
        mat->ncio(ncio, "AvI", {"dimA", "dimI"});
        ncio.flush();
    }

    {NcIO ncio(ofname, 'a', nocompress);
        printf("---- Generating EvI\n");
        auto mat(rm->matrix_d("EvI", {&dimE, &dimI}, params));
        mat->ncio(ncio, "EvI", {"dimE", "dimI"});
        ncio.flush();
    }

    {NcIO ncio(ofname, 'a', nocompress);
        printf("---- Generating IvE\n");
        auto mat(rm->matrix_d("IvE", {&dimI, &dimE}, params));
        mat->ncio(ncio, "IvE", {"dimI", "dimE"});
        ncio.flush();

        // Save smaller / more wieldly display version of the matrix
        auto mat2(make_I2vX(*mat, args, dimI2, dimI, dimE, params));
        mat.release();
        mat2.ncio(ncio, "I2vE", {"dimI2", "dimE"});
        ncio.flush();
    }
    {NcIO ncio(ofname, 'a', nocompress);
        printf("---- Generating IvA\n");
        std::unique_ptr<ibmisc::linear::Weighted_Eigen> mat(
            rm->matrix_d("IvA", {&dimI, &dimA}, params));
        mat->ncio(ncio, "IvA", {"dimI", "dimA"});
        ncio.flush();

        // Save smaller / more wieldly display version of the matrix
        auto mat2(make_I2vX(*mat, args, dimI2, dimI, dimA, params));
        mat.release();
        mat2.ncio(ncio, "I2vA", {"dimI2", "dimA"});
        ncio.flush();
    }

    {NcIO ncio(ofname, 'a', nocompress);
        printf("---- Generating AvE\n");
        auto mat(rm->matrix_d("AvE", {&dimA, &dimE}, params));
        mat->ncio(ncio, "AvE", {"dimA", "dimE"});
        ncio.flush();
    }

    // Store the dimensions
    printf("---- Storing Dimensions\n");
    {NcIO ncio(ofname, 'a', nocompress);
        NcVar ncv;

        ncv = dimA.ncio(ncio, "dimA");
        get_or_put_att(ncv, 'w', "shape", 
            &std::vector<int>{hspecA.jm, hspecA.im}[0], 2);
        ncv.putAtt("description", "GCM ('Atmosphere') Grid");

        ncv = dimE.ncio(ncio, "dimE");
        get_or_put_att(ncv, 'w', "shape", 
            &std::vector<int>{gcmA->nhc(), hspecA.jm, hspecA.im}[0], 3);
        ncv.putAtt("description", "Elevation Grid");

        ncv = dimI.ncio(ncio, "dimI");
        get_or_put_att(ncv, 'w', "shape", 
            &std::vector<int>{hspecI.jm, hspecI.im}[0], 2);
        ncv.putAtt("description", "Fine-scale ('Ice') Grid");

        ncv = dimI2.ncio(ncio, "dimI2");
        get_or_put_att(ncv, 'w', "shape", 
            &std::vector<int>{args.hspecI2.jm, args.hspecI2.im}[0], 2);
        ncv.putAtt("description", "Recuction of Fine-scale Grid, for easy plotting");

        ncio.flush();
    }

    printf("Done!\n");
}

void global_ec_section(FileLocator const &files, ParseArgs const &args, blitz::Array<double,2> const &elevmaskI)
{
    // Mismatched grids
    {
        auto gcmA(new_gcmA_mismatched(files, args, elevmaskI));
        global_ec_section(*gcmA, "mismatched", args, elevmaskI);
    }

    // Regular grids
    {
        HntrSpec const hspecA(make_hntrA(args.hspecO));
        auto gcmA(new_gcmA_standard(hspecA, "Atmosphere", args, elevmaskI));
        global_ec_section(*gcmA, "standard", args, elevmaskI);
    }
}



void write_chunk_makefile(
    std::string const &ofname,
    std::vector<string> const &arg_strings,
    std::vector<std::array<int,5>> const &chunks)
{
    ofstream fout;
    fout.open(ofname + ".mk", ofstream::out);
 
    fout << ".NOTPARALLEL:" << endl;    // Avoid memory blow-out

    // Name of all chunk files
    fout << ofname << " : " << ofname << ".mk";
    for (std::array<int,5> const &chunk : chunks)  {
        std::string chunkno(strprintf("%02d", chunk[0]));
        fout << " " << ofname << "-" << chunkno;
    }
    fout << endl;

    fout << "\tcombine_global_ec";
    for (std::array<int,5> const &chunk : chunks)  {
        std::string chunkno(strprintf("%02d", chunk[0]));
        fout << " " << ofname << "-" << chunkno;
    }
    fout << endl << endl;


    for (std::array<int,5> const &chunk : chunks) {
        std::string chunkno(strprintf("%02d", chunk[0]));
        fout << ofname << "-" << chunkno << " : " << ofname << ".mk" << endl << "\t";
        for (auto const &arg : arg_strings) fout << arg << " ";
        fout << "--runchunk " << chunk[0];
        for (int i=1; i<5; ++i) fout << "," << chunk[i];
        fout << "\n";
    }

    printf("Done writing chunk-generating makefile.  Run with the command:\n    make -f %s.mk\n", ofname.c_str());
}



int main(int argc, char **argv)
{
    everytrace_init();

    // Save args as C++ vector
    vector<string> arg_strings;
    for (int i=0; i<argc; ++i) arg_strings.push_back(string(argv[i]));

    ParseArgs args(argc, argv);
    std::string ofname(args.ofname);
    std::cout << args << endl;

    EnvSearchPath files("MODELE_FILE_PATH");

    auto &hspecI(args.hspecI);
    auto &hspecO(args.hspecO);

    // Check that I grid fits neatly into O grid
    // (simplifies our overlap "computation")
    int mult_i = hspecI.im / hspecO.im;
    int mult_j = hspecI.jm / hspecO.jm;
    if ((mult_i * hspecO.im != hspecI.im) || (mult_j * hspecO.jm != hspecI.jm)) {
        (*icebin_error)(-1,
            "Hntr grid (%dx%d) must be an even multiple of (%dx%d)",
            hspecI.im, hspecI.jm, hspecO.im, hspecO.jm);
    }

    // -----------------------------------------
    blitz::Array<double,2> fgiceO(hspecO.jm, hspecO.im);
    {
        // Allocate arrays
        blitz::Array<int16_t,2> fgiceI(hspecI.jm, hspecI.im);    // 0 or 1
        blitz::Array<int16_t,2> elevI(hspecI.jm, hspecI.im);

        // Read in ice extent and elevation
        {auto fname(files.locate(args.nc_fname));
            NcIO ncio(fname, 'r');
            ncio_blitz(ncio, fgiceI, args.fgiceI_vname, "short", {});
            ncio_blitz(ncio, elevI, args.elevI_vname, "short", {});
        }
        // -----------------------------------------

        // Generate fgiceO
        auto wtI(const_array(fgiceI.shape(), 1.0));
        Hntr hntrOvI(17.17, args.hspecO, args.hspecI);
        hntrOvI.regrid(wtI, fgiceI, fgiceO);

#if 0
        // Generate elevA
        // (This is a shortcut, compared to creating a GCMRegridder for A).
        HntrSpec const hspecA(make_hntrA(args.hspecO));
        blitz::Array<double,2> elevA(
        Hntr hntrAvI(17.17, hspecA, args.hspecI);
        hntrAvI.regrid(fgiceI, elevI, elevA);
#endif

    }


    // Allocate arrays
    blitz::Array<int16_t,2> fgiceI(hspecI.jm, hspecI.im);    // 0 or 1
    blitz::Array<int16_t,2> elevI(hspecI.jm, hspecI.im);

    // Read in ice extent and elevation
    {auto fname(files.locate(args.nc_fname));
        NcIO ncio(fname, 'r');
        ncio_blitz(ncio, fgiceI, args.fgiceI_vname, "short", {});
        ncio_blitz(ncio, elevI, args.elevI_vname, "short", {});
    }


    if (args.run_chunk) {
        // ============== Run just one chunk

        // Choose the ice to process on this chunk
        blitz::Array<double,2> elevmaskI(hspecI.jm, hspecI.im);
        elevmaskI = NaN;

        // Upper bound
        int const jO1 = args.chunk_range[1][0];
        int const iO1 = args.chunk_range[1][1];
        int const ijO1 = jO1 * hspecO.im + iO1;

        // Set up elevmaskI for the specified range of O grid cells
        int iO = args.chunk_range[0][1];    // Where we start scanning in fgiceO
        int jO=args.chunk_range[0][0];
        int ijO = jO * hspecO.im + iO;
        printf("BEGIN O(%d, %d)\n", jO, iO);
        for (; jO < args.chunk_range[1][0]; ++jO) {
            for (; iO < hspecO.im; ++iO, ++ijO) {
                if (ijO >= ijO1) goto endscan;    // Double break

                if (fgiceO(jO, iO) != 0) {
                    // Add these I grid cells to elevmaskI
                    for (int jI=jO*mult_j; jI<(jO+1)*mult_j; ++jI) {
                    for (int iI=iO*mult_i; iI<(iO+1)*mult_i; ++iI) {
                        if (fgiceI(jI,iI)) {
                            elevmaskI(jI,iI) = elevI(jI,iI);
                        }
                    }}
                }
            }
            iO = 0;
        }
    endscan: ;
        printf("END O(%d, %d)\n", jO, iO);
        fgiceI.free();
        elevI.free();

        // Process the chunk!
        global_ec_section(files, args, elevmaskI);
    } else {
        // ================== Create chunks to run

        std::vector<std::array<int,5>> chunks;

        // Loop over chunks
        int iO = 0;    // Where we start scanning in fgiceO
        int jO = 0;
        for (int chunkno=0; (jO < hspecO.jm) && (iO < hspecO.im); ++chunkno) {
            int nice=0;
            int const jO0 = jO;
            int const iO0 = iO;

            // Choose the ice to process on this chunk
            for (; jO < hspecO.jm; ++jO) {
                for (; iO < hspecO.im; ++iO) {
                    if (fgiceO(jO, iO) != 0) {

                        // Add these I grid cells to elevmaskI
                        for (int jI=jO*mult_j; jI<(jO+1)*mult_j; ++jI) {
                        for (int iI=iO*mult_i; iI<(iO+1)*mult_i; ++iI) {
                            if (fgiceI(jI,iI)) ++nice;
                        }}
                        if (nice >= chunk_size) goto endscan2;    // double break
                    }
                }
                iO = 0;
            }
        endscan2: ;
            printf("============= Chunk %d, nice=%d (%d %d) (%d %d)\n", chunkno, nice, jO0, iO0, jO, iO);
            chunks.push_back({chunkno, jO0, iO0, jO, iO});
        }


        // Create a makefile
        write_chunk_makefile(args.ofname, arg_strings, chunks);
    }

    return 0;
}


