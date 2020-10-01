#ifndef SMW_H
#define SMW_H
#include "pyfgc.h"
#include "blaze/util/Serialization.h"


struct SparseMatrixWrapper {
    SparseMatrixWrapper(std::string path, bool use_float=true) {
        blaze::Archive<std::ifstream> arch(path);
        if(use_float) arch >> this->getfloat();
                else  arch >> this->getdouble();
        perform([&arch](auto &x) {arch >> x;});
    }
    void tofile(std::string path) {
        blaze::Archive<std::ofstream> arch(path);
        perform([&arch](auto &x) {arch << x;});
    }
private:
    template<typename IndPtrT, typename IndicesT, typename Data>
    SparseMatrixWrapper(IndPtrT *indptr, IndicesT *indices, Data *data,
                  size_t nnz, uint32_t nfeat, uint32_t nitems, bool skip_empty=false, bool use_float=true) {
        if(use_float) {
            matrix_ = csc2sparse<float>(CSCMatrixView<IndPtrT, IndicesT, Data>(indptr, indices, data, nnz, nfeat, nitems), skip_empty);
            auto &m(getfloat());
            std::fprintf(stderr, "[%s] Produced float matrix of %zu/%zu with %zu nonzeros\n", __PRETTY_FUNCTION__, m.rows(), m.columns(), blaze::nonZeros(m));
            std::cerr << m;
        } else {
            matrix_ = csc2sparse<double>(CSCMatrixView<IndPtrT, IndicesT, Data>(indptr, indices, data, nnz, nfeat, nitems), skip_empty);
            auto &m(getdouble());
            std::fprintf(stderr, "[%s] Produced double matrix of %zu/%zu with %zu nonzeros\n", __PRETTY_FUNCTION__, m.rows(), m.columns(), blaze::nonZeros(m));
            std::cerr << m;
        }
    }
    template<typename FT>
    SparseMatrixWrapper(blz::SM<FT> &&mat): matrix_(std::move(mat)) {}
public:
    blz::SM<float> &getfloat() { return std::get<SMF>(matrix_);}
    const blz::SM<float> &getfloat() const { return std::get<SMF>(matrix_);}
    blz::SM<double> &getdouble() { return std::get<SMD>(matrix_);}
    const blz::SM<double> &getdouble() const { return std::get<SMD>(matrix_);}
    template<typename FT>
    SparseMatrixWrapper& operator=(blz::SM<FT> &&mat) {
        if(is_float()) {
            matrix_ = std::move(mat);
        } else {
            {
                SMD tmpmat(std::move(std::get<SMD>(matrix_)));
            }
        }
        return *this;
    }
    size_t nnz() const {
        size_t ret;
        perform([&](auto &x) {ret = blz::nonZeros(x);});
        return ret;
    }
    size_t columns() const {
        size_t ret;
        perform([&](auto &x) {ret = x.columns();});
        return ret;
    }
    size_t rows() const {
        size_t ret;
        perform([&](auto &x) {ret = x.rows();});
        return ret;
    }
    template<typename IpT, typename IdxT, typename DataT>
    SparseMatrixWrapper(IpT *indptr, IdxT *idx, DataT *data, size_t xdim, size_t ydim, size_t nnz, bool use_float=true, bool skip_empty=true) {
        if(use_float)
            matrix_ = csc2sparse<float>(CSCMatrixView<IpT, IdxT, DataT>(indptr, idx, data, nnz, ydim, xdim));
        else
            matrix_ = csc2sparse<double>(CSCMatrixView<IpT, IdxT, DataT>(indptr, idx, data, nnz, ydim, xdim));
    }
    SparseMatrixWrapper(py::object spmat, py::object skip_empty_py, py::object use_float_py) {
        py::array indices = spmat.attr("indices"), indptr = spmat.attr("indptr"), data = spmat.attr("data");
        py::tuple shape = py::cast<py::tuple>(spmat.attr("shape"));
        const bool use_float = py::cast<bool>(use_float_py), skip_empty = py::cast<bool>(skip_empty_py);
        size_t xdim = py::cast<size_t>(shape[0]), ydim = py::cast<size_t>(shape[1]);
        size_t nnz = py::cast<size_t>(spmat.attr("nnz"));
        auto indbuf = indices.request(), indpbuf = indptr.request(), databuf = data.request();
        void *datptr = databuf.ptr, *indptrptr = indpbuf.ptr, *indicesptr = indbuf.ptr;

#define __DISPATCH(T1, T2, T3) do { \
        if(use_float) {\
            if(databuf.readonly || indbuf.readonly) {\
                DBG_ONLY(std::fprintf(stderr, "Read only floats\n");) \
                matrix_ = csc2sparse<float>(CSCMatrixView<T1, const T2, const T3>(reinterpret_cast<T1 *>(indptrptr), reinterpret_cast<const T2 *>(const_cast<const void *>(indicesptr)), reinterpret_cast<const T3 *>(const_cast<const void *>(datptr)), nnz, ydim, xdim), skip_empty); \
            } else { \
                DBG_ONLY(std::fprintf(stderr, "Const reading of floats\n");) \
                matrix_ = csc2sparse<float>(CSCMatrixView<T1, T2, T3>(reinterpret_cast<T1 *>(indptrptr), reinterpret_cast<T2 *>(indicesptr), reinterpret_cast<T3 *>(datptr), nnz, ydim, xdim), skip_empty); \
            }\
        } else { \
            if(databuf.readonly || indbuf.readonly) {\
                DBG_ONLY(std::fprintf(stderr, "Read only reading of doubles\n");) \
                matrix_ = csc2sparse<double>(CSCMatrixView<T1, const T2, const T3>(reinterpret_cast<T1 *>(indptrptr), reinterpret_cast<const T2 *>(const_cast<const void *>(indicesptr)), reinterpret_cast<const T3 *>(const_cast<const void *>(datptr)), nnz, ydim, xdim), skip_empty); \
            } else { \
                DBG_ONLY(std::fprintf(stderr, "Const reading of doubles\n");) \
                matrix_ = csc2sparse<double>(CSCMatrixView<T1, T2, T3>(reinterpret_cast<T1 *>(indptrptr), reinterpret_cast<T2 *>(indicesptr), reinterpret_cast<T3 *>(datptr), nnz, ydim, xdim), skip_empty); \
            }\
        }\
        return; \
    } while(0)
#define __DISPATCH_IF(T1, T2, T3) do { \
                if(py::format_descriptor<T3>::format() == databuf.format) { \
                    __DISPATCH(T1, T2, T3); \
                } } while(0)

#define __DISPATCH_ALL_IF(T1, T2) do {\
     __DISPATCH_IF(T1, T2, uint32_t);\
     __DISPATCH_IF(T1, T2, uint64_t);\
     __DISPATCH_IF(T1, T2, int32_t);\
     __DISPATCH_IF(T1, T2, int64_t);\
     __DISPATCH_IF(T1, T2, float);\
     __DISPATCH_IF(T1, T2, double);\
    } while(0)
        if(indbuf.itemsize == 4) {
            if(indpbuf.itemsize == 4) {
                __DISPATCH_ALL_IF(uint32_t, uint32_t);
            } else {
                __DISPATCH_ALL_IF(uint64_t, uint32_t);
            }
        } else {
            assert(indbuf.itemsize == 8);
            if(indpbuf.itemsize == 4) {
                __DISPATCH_ALL_IF(uint32_t, uint64_t);
            } else {
                __DISPATCH_ALL_IF(uint64_t, uint64_t);
            }
        }
        throw std::runtime_error("Unexpected type");
#undef __DISPATCH_ALL_IF
#undef __DISPATCH_IF
#undef __DISPATCH
    }

    std::variant<SMF, SMD> matrix_;
    bool is_float() const {
        assert(is_float() != is_double());
        return std::holds_alternative<SMF>(matrix_);
    }
    bool is_double() const {
        return std::holds_alternative<SMD>(matrix_);
    }
    template<typename Func>
    void perform(const Func &func) {
        if(is_float()) func(std::get<SMF>(matrix_));
        else           func(std::get<SMD>(matrix_));
    }
    template<typename Func>
    void perform(const Func &func) const {
        if(is_float()) func(std::get<SMF>(matrix_));
        else           func(std::get<SMD>(matrix_));
    }
    std::vector<std::pair<uint32_t, double>> row2tups(size_t r) const {
        if(r > rows()) throw std::invalid_argument("Cannot get tuples from a row that dne");
        std::vector<std::pair<uint32_t, double>> ret;
        perform([&ret,r](const auto &x) {
            for(const auto &pair: row(x, r))
                ret.emplace_back(pair.index(), pair.value());
        });
        return ret;
    }
    std::pair<void *, bool> get_opaque() {
        return {is_float() ? static_cast<void *>(&std::get<SMF>(matrix_)): static_cast<void *>(&std::get<SMD>(matrix_)),
                is_float()};
    }
};

dist::DissimilarityMeasure assure_dm(py::object obj);

#endif