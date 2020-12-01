#ifndef PYCLUSTER_HEADER_H__
#define PYCLUSTER_HEADER_H__
#include "pyfgc.h"
#include "smw.h"
#include "pyhelpers.h"
#include "pycsparse.h"
using blaze::unaligned;
using blaze::unpadded;

py::object func1(const SparseMatrixWrapper &smw, py::int_ k, double beta,
                 py::object msr, py::object weights, double eps,
                 int ntimes, uint64_t seed, int lspprounds, int kmcrounds, uint64_t kmeansmaxiter);
py::object func1(const PyCSparseMatrix &smw, py::int_ k, double beta,
                 py::object msr, py::object weights, double eps,
                 int ntimes, uint64_t seed, int lspprounds, int kmcrounds, uint64_t kmeansmaxiter);

template<typename Matrix, typename WFT, typename CtrT, typename AsnT=blz::DV<uint32_t>, typename CostsT=blz::DV<double>>
py::dict cpp_pycluster_from_centers(const Matrix &mat, unsigned int k, double beta,
               dist::DissimilarityMeasure measure,
               std::vector<CtrT> &ctrs,
               AsnT &asn, CostsT &costs,
               WFT *weights,
               double eps,
               size_t kmeansmaxiter,
               Py_ssize_t mbsize,
               Py_ssize_t ncheckins,
               Py_ssize_t reseed_count,
               bool with_rep,
               Py_ssize_t seed)
{
    std::fprintf(stderr, "[%s]\n", __PRETTY_FUNCTION__);
    if(k != ctrs.size()) {
        throw std::invalid_argument(std::string("k ") + std::to_string(k) + "!=" + std::to_string(ctrs.size()) + ", ctrs.size()");
    }
    using FT = double;
    blz::DV<FT> prior{FT(beta)};
    std::tuple<double, double, size_t> clusterret;
    if(mbsize < 0) {
        clusterret = perform_hard_clustering(mat, measure, prior, ctrs, asn, costs, weights, eps, kmeansmaxiter);
    } else {
        if(ncheckins < 0) ncheckins = 10;
        Py_ssize_t checkin_freq = (kmeansmaxiter + ncheckins - 1) / ncheckins;
        clusterret = perform_hard_minibatch_clustering(mat, measure, prior, ctrs, asn, costs, weights,
                                                       mbsize, kmeansmaxiter, checkin_freq, reseed_count, with_rep, seed);
    }
    auto &[initcost, finalcost, numiter]  = clusterret;
    auto pyctrs = centers2pylist(ctrs);
    auto pycosts = vec2fnp<decltype(costs), float> (costs);
    auto pyasn = vec2fnp<decltype(asn), uint32_t>(asn);
    return py::dict("initcost"_a = initcost, "finalcost"_a = finalcost, "numiter"_a = numiter,
                    "centers"_a = pyctrs, "costs"_a = pycosts, "asn"_a=pyasn);
}

template<typename Matrix, typename WFT, typename CtrT, typename AsnT=blz::DV<uint32_t>, typename CostsT=blz::DV<double>>
py::dict cpp_pycluster_from_centers_base(const Matrix &mat, unsigned int k, double beta,
               dist::DissimilarityMeasure measure,
               std::vector<CtrT> &ctrs,
               AsnT &asn, CostsT &costs,
               WFT *weights,
               double eps,
               size_t kmeansmaxiter,
               Py_ssize_t mbsize,
               Py_ssize_t ncheckins,
               Py_ssize_t reseed_count,
               bool with_rep,
               Py_ssize_t seed)
{
    py::dict ret;
    mat.perform([&](auto &x) {ret = cpp_pycluster_from_centers(x, k, beta, measure, ctrs, asn, costs, weights, eps, kmeansmaxiter, mbsize, ncheckins, reseed_count, with_rep, seed);});
    return ret;
}

template<typename Matrix, typename WFT, typename FT=double>
py::dict cpp_pycluster(const Matrix &mat, unsigned int k, double beta,
               dist::DissimilarityMeasure measure,
               WFT *weights=static_cast<WFT *>(nullptr),
               double eps=1e-10,
               int ntimes=2,
               uint64_t seed=13,
               unsigned lspprounds=0,
               bool use_exponential_skips=false,
               size_t kmcrounds=1000,
               size_t kmeansmaxiter=1000,
               Py_ssize_t mbsize=-1,
               Py_ssize_t ncheckins=-1,
               Py_ssize_t reseed_count=-1,
               bool with_rep=true)
{
    std::fprintf(stderr, "[%s] beginning cpp_pycluster\n", __PRETTY_FUNCTION__);
    blz::DV<FT> prior{FT(beta)};
    const FT psum = beta * mat.columns();
    if(measure == dist::L1 || measure == dist::L2 || measure == dist::BHATTACHARYYA_METRIC) {
        std::fprintf(stderr, "D2 sampling may not provide a bicriteria approximation alone. TODO: use more expensive metric clustering for better objective functions.\n");
    }
    wy::WyRand<uint32_t> rng(seed);
    using ET = typename Matrix::ElementType;
    using MsrType = std::conditional_t<std::is_floating_point_v<ET>, ET, std::conditional_t<(sizeof(ET) <= 4), float, double>>;
    auto functor = [&](const auto &x, const auto &y) {
        return cmp::msr_with_prior<MsrType>(measure, y, x, prior, psum, sum(y), sum(x));
    };
    std::fprintf(stderr, "About to try to get initial centers\n");
    auto initial_sol = repeatedly_get_initial_centers(mat, rng, k, kmcrounds, ntimes, lspprounds, use_exponential_skips, functor);
    std::fprintf(stderr, "Got initial centers\n");
    auto &[idx, asn, costs] = initial_sol;
    std::vector<blz::CompressedVector<FT, blz::rowVector>> centers(k);
    for(unsigned i = 0; i < k; ++i) {
        assign(centers[i], row(mat, idx[i]));
    }
    return cpp_pycluster_from_centers(mat, k, beta, measure, centers, asn, costs, weights, eps, kmeansmaxiter, mbsize, ncheckins, reseed_count, with_rep, seed);
}

template<typename Matrix, typename WFT>
py::dict pycluster(const Matrix &smw, int k, double beta,
               dist::DissimilarityMeasure measure,
               WFT *weights,
               double eps=1e-10,
               int ntimes=3,
               uint64_t seed = 13,
               unsigned lspprounds=0,
               size_t kmcrounds=1000,
               size_t kmeansmaxiter=1000,
               Py_ssize_t mbsize=-1,
               Py_ssize_t ncheckins=-1,
               Py_ssize_t reseed_count=-1,
               bool with_rep=true)
{
    assert(k >= 1);
    assert(beta > 0.);
    py::dict retdict;
    smw.perform([&](auto &x) {retdict = cpp_pycluster(x, k, beta, measure, weights, eps, ntimes, seed, lspprounds, kmcrounds, kmeansmaxiter, mbsize, ncheckins, reseed_count, with_rep);});
    return retdict;
}


template<typename Matrix>
py::object __py_cluster_from_centers(const Matrix &smw,
                    py::object centers, double beta,
                    py::object msr, py::object weights, double eps,
                    uint64_t kmeansmaxiter,
                    //size_t kmcrounds, int ntimes, int lspprounds,
                    uint64_t seed,
                    Py_ssize_t mbsize, Py_ssize_t ncheckins,
                    Py_ssize_t reseed_count, bool with_rep)
{
    blz::DV<double> prior{double(beta)};
    const dist::DissimilarityMeasure measure = assure_dm(msr);
    std::vector<blz::CompressedVector<float, blz::rowVector>> dvecs = obj2dvec(centers);
    const unsigned long long k = dvecs.size();
    blz::DV<uint32_t> asn(smw.rows());
    if(k > 0xFFFFFFFFull) throw std::invalid_argument("k must be < 4.3 billion to fit into a uint32_t");
    const auto psum = beta * smw.columns();
    blz::DV<double> centersums = blaze::generate(k, [&dvecs](auto x) {
        return blz::sum(dvecs[x]);
    });
    blz::DV<float> costs;
    smw.perform([&](auto &mat) {
        using ET = typename std::decay_t<decltype(mat)>::ElementType;
        using MsrType = std::conditional_t<std::is_floating_point_v<ET>, ET, std::conditional_t<(sizeof(ET) <= 4), float, double>>;
        costs = blaze::generate(mat.rows(), [&](size_t idx) {
            double bestcost;
            uint32_t bestind;
                auto r = row(mat, idx);
                const double rsum = sum(r);
                bestind = 0;
                auto c = cmp::msr_with_prior<MsrType>(measure, r, dvecs[0], prior, psum, rsum, centersums[0]);
                for(unsigned j = 1; j < k; ++j) {
                    auto nextc = cmp::msr_with_prior<MsrType>(measure, r, dvecs[j], prior, psum, rsum, centersums[j]);
                    if(nextc < c)
                        c = nextc, bestind = j;
                }
                bestcost = c;
            asn[idx] = bestind;
            return bestcost;
        });
    });
    if(weights.is_none()) {
        return cpp_pycluster_from_centers_base(smw, k, beta, measure, dvecs, asn, costs, (blz::DV<double> *)nullptr, eps, kmeansmaxiter, mbsize, ncheckins, reseed_count, with_rep, seed);
    }
    auto weightinfo = py::cast<py::array>(weights).request();
    if(weightinfo.format.size() != 1) throw std::invalid_argument("Weights must be 0 or contain a fundamental type");
    switch(weightinfo.format[0]) {
#define CASE_MCR(x, type) case x: {\
        auto cv = blz::make_cv((type *)weightinfo.ptr, costs.size());\
        return cpp_pycluster_from_centers_base(smw, k, beta, measure, dvecs, asn, costs, &cv, eps, kmeansmaxiter, mbsize, ncheckins, reseed_count, with_rep, seed); \
        } break
        CASE_MCR('f', float);
        CASE_MCR('d', double);
        default: throw std::invalid_argument(std::string("Invalid weights value type: ") + weightinfo.format);
#undef CASE_MCR
    }
    throw std::invalid_argument("Weights were not float, double, or None.");
    return py::none();
}


#endif
