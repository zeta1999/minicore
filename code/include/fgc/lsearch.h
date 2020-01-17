#pragma once
#ifndef FGC_LOCAL_SEARCH_H__
#define FGC_LOCAL_SEARCH_H__
#include "fgc/graph.h"
#include "fgc/diskmat.h"
#include "fgc/kcenter.h"
#include "pdqsort/pdqsort.h"

/*
 * In this file, we use the local search heuristic for k-median.
 * Originally described in "Local Search Heuristics for k-median and Facility Location Problems",
 * Vijay Arya, Naveen Garg, Rohit Khandekar, Adam Meyerson, Kamesh Munagala, Vinayaka Pandit
 * (http://theory.stanford.edu/~kamesh/lsearch.pdf)
 */

namespace fgc {

template<typename Graph, typename VType=std::vector<typename boost::graph_traits<Graph>::vectex_descriptor>>
DiskMat<typename Graph::edge_property_type::value_type> graph2diskmat(const Graph &x, std::string path, VType *sources=nullptr) {
    using ST = std::decay_t<decltype((*sources)[0])>;
    static_assert(std::is_arithmetic<typename Graph::edge_property_type::value_type>::value, "This should be floating point, or at least arithmetic");
    using FT = typename Graph::edge_property_type::value_type;
    const size_t nv = boost::num_vertices(x), nrows = sources ? sources->size(): nv;
    DiskMat<FT> ret(nrows, nv, path);
    typename boost::graph_traits<Graph>::vertex_iterator vertices = boost::vertices(x).first;
    OMP_PFOR
    for(size_t i = 0; i < nrows; ++i) {
        auto mr = row(~ret, i);
        auto vtx = sources ? (*sources)[i]: ST(i);
        boost::dijkstra_shortest_paths(x, vtx, distance_map(&mr[0]));
    }
    assert((~ret).rows() == nrows && (~ret).columns() == nv);
    return ret;
}

#if 0
template<typename Mat, typename Norm>
struct MatrixIndexNorm {
    const Mat mat_;
    const Norm norm_;
    template<typename Mat>
    MatrixMetric(const Mat &mat): mat_(mat) {}
    template<typename AT>
    auto operator()(size_t i, size_t j) const {
        return mat_(i, j);
    }
};
#endif

template<typename MatType, typename WFT=float, typename IType=std::uint32_t>
struct LocalKMedSearcher {
    using SolType = blaze::SmallArray<IType, 16>;
    using value_type = typename MatType::ElementType;


    const MatType &mat_;
    shared::flat_hash_set<IType> sol_;
    blz::DV<IType> assignments_;
    double current_cost_;
    const double eps_;
    IType k_;
    const size_t nr_, nc_;

    // Constructors

    LocalKMedSearcher(const LocalKMedSearcher &o) = default;
    LocalKMedSearcher(LocalKMedSearcher &&o) {
        auto ptr = reinterpret_cast<const uint8_t *>(this);
        std::memset(ptr, 0, sizeof(*this));
        std::swap_ranges(ptr, ptr + sizeof(*this), reinterpret_cast<const uint8_t *>(std::addressof(o)));
    }
    LocalKMedSearcher(const MatType &mat, unsigned k, double eps=0.01, uint64_t seed=0):
        mat_(mat), assignments_(mat.columns(), 0),
        // center_indices_(k),
        //costs_(mat.columns(), std::numeric_limits<value_type>::max()),
        //counts_(k),
        current_cost_(std::numeric_limits<value_type>::max()),
        eps_(eps),
        k_(k), nr_(mat.rows()), nc_(mat.columns())
    {
        sol_.reserve(k);
        reseed(seed, true);
    }

    void reseed(uint64_t seed, bool do_kcenter=false) {
        assignments_ = 0;
        current_cost_ = std::numeric_limits<value_type>::max();
        wy::WyRand<IType, 2> rng(seed);
        sol_.clear();
        if(do_kcenter) {
            auto rowits = rowiterator(mat_);
            auto approx = coresets::kcenter_greedy_2approx(rowits.begin(), rowits.end(), rng, k_, MatrixLookup());
            for(const auto c: approx) sol_.insert(c);
        } else {
            while(sol_.size() < k_)
                sol_.insert(rng() % mat_.rows());
        }
        assign();
    }

    template<typename Container>
    double cost_for_sol(const Container &c) {
        double ret = 0.;
        OMP_PRAGMA("omp parallel for reduction(+:ret)")
        for(size_t i = 0; i < mat_.columns(); ++i) {
            auto col = column(mat_, i);
            auto it = c.begin();
            value_type minv = col[*it++];
            while(it != c.end())
                minv = std::min(col[*it++], minv);
            ret += minv;
        }
        return ret;
    }

    // Setup/Utilities

    void assign() {
        assert(assignments_.size() == nc_);
        assert(sol_.size() == k_);
        for(const auto center: sol_) {
            auto r = row(mat_, center, blaze::unchecked);
            OMP_PFOR
            for(size_t ci = 0; ci < nc_; ++ci) {
                if(const auto newcost = r[ci];
                   newcost < mat_(assignments_[ci], ci))
                {
                   assignments_[ci] = center;
                }
            }
        }
        current_cost_ = cost_for_sol(sol_);
    }

    double evaluate_swap(IType newcenter, IType oldcenter) const {
        assert(newcenter < mat_.rows());
        assert(oldcenter < mat_.rows());
        auto newr = row(mat_, newcenter, blaze::unchecked);
        double potential_gain = 0.;
        OMP_PRAGMA("omp parallel for reduction(+:potential_gain)")
        for(size_t i = 0; i < nc_; ++i) {
            //if(assignments_[i] != oldcenter) continue; // Only points already assigned to
            //assert(costs_[i] == mat_(assignments_[i], i));
            auto asn = assignments_[i];
            if(asn == oldcenter || newr[i] < mat_(asn, i)) {
                potential_gain += mat_(asn, i) - newr[i];
            }
        }
        return potential_gain;
    }

    // Getters
    auto k() const {
        return k_;
    }

    void recalculate() {
        assignments_ = *sol_.begin();

        for(auto it = sol_.begin(); ++it != sol_.end();) {
            const auto center = *it;
            auto crow = row(mat_, center, blaze::unchecked);
            OMP_PFOR
            for(size_t i = 0; i < nc_; ++i) {
                if(const auto cost(crow[i]); cost < mat_(assignments_[i], i))
                    assignments_[i] = center;
            }
        }
#ifndef NDEBUG
        for(const auto asn: assignments_)
            assert(std::find(sol_.begin(), sol_.end(), asn) != sol_.end());
#endif
        double newcost = 0.;
        OMP_PRAGMA("omp parallel for reduction(+:newcost)")
        for(size_t i = 0; i < nc_; ++i)
            newcost += mat_(assignments_[i], i);


        std::fprintf(stderr, "newcost: %f. old cost: %f\n", newcost, current_cost_);
        assert(newcost <= current_cost_);
        current_cost_ = newcost;
    }

    void run() {

        double diffthresh = current_cost_ / k_ * eps_;
        size_t total = 0;
        {
            next:
            for(const auto oldcenter: sol_) {
                for(size_t pi = 0; pi < nr_; ++pi) {
                    if(sol_.find(pi) != sol_.end()) continue;
                    const auto val = evaluate_swap(pi, oldcenter);
                    if(val > diffthresh) {
                        std::fprintf(stderr, "Swapping %zu for %u. Swap number %zu\n", pi, oldcenter, total + 1);

                        sol_.erase(oldcenter);
                        sol_.insert(pi);

                        recalculate();
                        diffthresh = current_cost_ / k_ * eps_;
                        ++total;
                        goto next; // Meaning we've swapped this guy out and will pick another one.
                    }
                }
            }
        }
        std::fprintf(stderr, "Finished in %zu swaps by exhausting all potential improvements. Final cost: %f\n",
                     total, current_cost_);
    }
};

template<typename Mat, typename FT=float, typename IType=std::uint32_t>
auto make_kmed_lsearcher(const Mat &mat, unsigned k, double eps=0.01, uint64_t seed=0) {
    return LocalKMedSearcher<Mat, FT, IType>(mat, k, eps, seed);
}

template<typename MT, blaze::AlignmentFlag AF, bool SO, bool DF, typename IType=std::uint32_t>
auto make_kmed_lsearcher(const blaze::Submatrix<MT, AF, SO, DF> &mat, unsigned k, double eps=0.01, uint64_t seed=0) {
    using FT = typename MT::ElementType;
    blaze::CustomMatrix<FT, AF, blaze::IsPadded<MT>::value, SO> custom(const_cast<FT *>(mat.data()), mat.rows(), mat.columns(), mat.spacing());
    return LocalKMedSearcher<decltype(custom), FT, IType>(custom, k, eps, seed);
}

} // fgc

#endif /* FGC_LOCAL_SEARCH_H__ */
