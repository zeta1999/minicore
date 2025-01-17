#include "smw.h"
#include "pyfgc.h"
#include <sstream>
#include <map>

using blz::unchecked;

using smw_t = SparseMatrixWrapper;

py::object run_kmpp_noso(const SparseMatrixWrapper &smw, py::object msr, py::int_ k, double gamma_beta, uint64_t seed, unsigned nkmc, unsigned ntimes,
                         Py_ssize_t lspp, bool use_exponential_skips, py::ssize_t n_local_trials,
                         py::object weights) {
    return py_kmeanspp_noso(smw, msr, k, gamma_beta, seed, nkmc, ntimes, lspp, use_exponential_skips, n_local_trials, weights);
}

dist::DissimilarityMeasure assure_dm(py::object obj) {
    dist::DissimilarityMeasure ret;
    if(py::isinstance<py::str>(obj)) {
        auto s = py::cast<std::string>(obj);
        ret = dist::str2msr(s);
    } else if(py::isinstance<py::int_>(obj)) {
        ret = static_cast<dist::DissimilarityMeasure>(obj.cast<Py_ssize_t>());
    } else {
        throw std::invalid_argument("assure_dm received object containing neither a string or an integer.");
    }
    if(!dist::is_valid_measure(ret)) throw std::invalid_argument(std::to_string(ret) + " is not a valid measure");
    return ret;
}
void init_smw(py::module &m) {
#if 1
    py::class_<SparseMatrixWrapper>(m, "SparseMatrixWrapper")
    .def(py::init<>())
    .def(py::init<py::object, py::object, py::object>(), py::arg("sparray"), py::arg("skip_empty")=false, py::arg("use_float")=true)
    .def("is_float", [](SparseMatrixWrapper &wrap) {
        return wrap.is_float();
    })
    .def("tofile", [](const SparseMatrixWrapper &wrap, std::string path) {
        wrap.tofile(path);
    })
    .def("tocsr", [](const SparseMatrixWrapper &lhs) {
        py::array_t<py::ssize_t> indptr(lhs.rows() + 1);
        py::array_t<int32_t> indices(lhs.nnz());
        py::array_t<double> data(lhs.nnz());
        py::array_t<py::ssize_t> shape(2);
        auto shapep = (py::ssize_t *)shape.request().ptr;
        shapep[0] = lhs.rows(); shapep[1] = lhs.columns();
        auto ipp = (py::ssize_t *)indptr.request().ptr;
        auto ip = (int32_t *)indices.request().ptr;
        auto fp = (double *)data.request().ptr;
        ipp[0] = 0;
        lhs.perform([&](auto &mat) {
            for(size_t i = 0; i < mat.rows(); ++i) {
                auto r = row(mat, i);
                ipp[i + 1] = ipp[i] + nonZeros(r);
                for(const auto &pair: r) {
                    *ip++ = pair.index();
                    *fp++ = pair.value();
                }
            }
        });
        return py::make_tuple(py::make_tuple(data, indices, indptr), shape);
    })
    .def("fromfile", [](SparseMatrixWrapper &wrap, std::string path) {
        wrap.fromfile(path);
    })
    .def("is_double", [](SparseMatrixWrapper &wrap) {
        return wrap.is_double();
    }).def("transpose_", [](SparseMatrixWrapper &wrap) {
        wrap.perform([](auto &x){x.transpose();});
    }).def("emit", [](SparseMatrixWrapper &wrap, bool to_stdout) {
        auto func = [to_stdout](auto &x) {
            if(to_stdout) std::cout << x;
            else          std::cerr << x;
        };
        wrap.perform(func);
    }, py::arg("to_stdout")=false)
    .def("__str__", [](SparseMatrixWrapper &wrap) {
        char buf[1024];
        return std::string(buf, std::sprintf(buf, "Matrix of %zu/%zu elements of %s, %zu nonzeros", wrap.rows(), wrap.columns(), wrap.is_float() ? "float32": "double", wrap.nnz()));
    })
    .def("__repr__", [](SparseMatrixWrapper &wrap) {
        char buf[1024];
        return std::string(buf, std::sprintf(buf, "Matrix of %zu/%zu elements of %s, %zu nonzeros", wrap.rows(), wrap.columns(), wrap.is_float() ? "float32": "double", wrap.nnz()));
    }).def("rows", [](SparseMatrixWrapper &wrap) {return wrap.rows();})
    .def("columns", [](SparseMatrixWrapper &wrap) {return wrap.columns();})
    .def("nonzeros", [](SparseMatrixWrapper &wrap) {return wrap.nnz();})
    .def("rowsel", [](SparseMatrixWrapper &smw, py::array idx) {
        auto info = idx.request();
        switch(info.format[0]) {
            case 'd': case 'f': throw std::invalid_argument("Unexpected type");
        }
        py::object ret;
        if(smw.is_float()) {
            py::array_t<float> arr(std::vector<size_t>{size_t(info.size), smw.columns()});
            auto ari = arr.request();
            auto mat = blaze::CustomMatrix<float, blaze::unaligned, blaze::unpadded> ((float *)ari.ptr, info.size, smw.columns());
            switch(info.itemsize) {
                case 8: {
                    mat = rows(smw.getfloat(), (uint64_t *)info.ptr, info.size); break;
                }
                case 4: {
                    mat = rows(smw.getfloat(), (uint32_t *)info.ptr, info.size); break;
                }
                default: throw std::invalid_argument("rows must be integral and of 4 or 8 bytes");
            }
            ret = arr;
        } else {
            py::array_t<double> arr(std::vector<size_t>{size_t(info.size), smw.columns()});
            auto ari = arr.request();
            auto mat = blaze::CustomMatrix<double, blaze::unaligned, blaze::unpadded>((double *)ari.ptr, info.size, smw.columns());
            switch(info.itemsize) {
                case 8: {
                    mat = rows(smw.getfloat(), (uint64_t *)info.ptr, info.size); break;
                }
                case 4: {
                    mat = rows(smw.getfloat(), (uint32_t *)info.ptr, info.size); break;
                }
                default: throw std::invalid_argument("rows must be integral and of 4 or 8 bytes");
            }
            ret = arr;
        }
        return ret;
    }, "Select rows in numpy array idx from matrix smw, returning as dense numpy arrays", py::arg("idx"))
    .def("tofile", [](SparseMatrixWrapper &lhs, std::string path) {
        lhs.tofile(path);
    }, py::arg("path"))
    .def("submatrix", [](const SparseMatrixWrapper &wrap, py::object rowsel, py::object columnsel) -> SparseMatrixWrapper {
        if(columnsel.is_none()) {
            if(rowsel.is_none()) {
                // Copy over
                if(wrap.is_float()) {return SparseMatrixWrapper(blz::SM<float>(wrap.getfloat()));}
                return SparseMatrixWrapper(blz::SM<double>(wrap.getdouble()));
            }
            auto inf = py::cast<py::array>(rowsel).request();
            if(inf.format.size() != 1) throw std::invalid_argument("Wrong dtype");
            switch(inf.format[0]) {
#define DTCASE(chr, type) case chr: {if(wrap.is_float()) return blz::SM<float>(blaze::rows(wrap.getfloat(), (type *)inf.ptr, inf.size));\
                                     else               return blz::SM<double>(blaze::rows(wrap.getdouble(), (type *)inf.ptr, inf.size));}
            DTCASE('L', uint64_t) DTCASE('I', uint32_t) DTCASE('H', uint16_t) DTCASE('B', uint8_t)
            DTCASE('l', int64_t) DTCASE('i', int32_t) DTCASE('h', int16_t) DTCASE('b', int8_t)
 #undef DTCASE
            default: throw std::invalid_argument("Wrong dtype");
            }
        } else if(rowsel.is_none()) {
            auto inf = py::cast<py::array>(columnsel).request();
            if(inf.format.size() != 1) throw std::invalid_argument("Wrong dtype");
            switch(inf.format[0]) {
#define DTCASE(chr, type) case chr: {if(wrap.is_float()) return blz::SM<float>(blaze::columns(wrap.getfloat(), (type *)inf.ptr, inf.size));\
                                     else               return blz::SM<double>(blaze::columns(wrap.getdouble(), (type *)inf.ptr, inf.size));}
            DTCASE('L', uint64_t) DTCASE('I', uint32_t) DTCASE('H', uint16_t) DTCASE('B', uint8_t)
            DTCASE('l', int64_t) DTCASE('i', int32_t) DTCASE('h', int16_t) DTCASE('b', int8_t)
 #undef DTCASE
                default: throw std::invalid_argument("Wrong dtype");
            }
        } else {
            auto rowarr = py::cast<py::array>(rowsel), columnarr = py::cast<py::array>(columnsel);
            auto rinf  = rowarr.request(), cinf = columnarr.request();
            if(rinf.format != cinf.format) throw std::invalid_argument("row and column selection should be the same integral types");
            if(rinf.format.size() != 1) throw std::invalid_argument("rinf format size must be 1");
            switch(rinf.format[0]) {
#define DTCASE(chr, type) case chr: if(wrap.is_float()) return blz::SM<float>(columns(rows(wrap.getfloat(), (type *)rinf.ptr, rinf.size), (type *)cinf.ptr, cinf.size));\
                                    else                return blz::SM<double>(columns(rows(wrap.getdouble(), (type *)rinf.ptr, rinf.size), (type *)cinf.ptr, cinf.size));
                DTCASE('L', uint64_t) DTCASE('I', uint32_t) DTCASE('H', uint16_t) DTCASE('B', uint8_t)
                DTCASE('l', int64_t) DTCASE('i', int32_t) DTCASE('h', int16_t) DTCASE('b', int8_t)
#undef DTCASE
                default: throw std::invalid_argument("Wrong dtype");
            }
        }
    }, py::arg("rowsel") = py::none(), py::arg("columnsel") = py::none())
    .def("variance", [](SparseMatrixWrapper &wrap, int byrow, bool usefloat) -> py::object
    {
        switch(byrow) {case -1: case 0: case 1: break; default: throw std::invalid_argument("byrow must be -1 (total sum), 0 (by column) or by row (1)");}
        if(byrow == -1) {
            double ret;
            wrap.perform([&ret](const auto &x) {ret = blaze::var(x);});
            return py::float_(ret);
        }
        py::array ret;
        Py_ssize_t nelem = byrow ? wrap.rows(): wrap.columns();
        if(usefloat) ret = py::array_t<float>(nelem);
                else ret = py::array_t<double>(nelem);
        auto bi = ret.request();
        auto ptr = bi.ptr;
        if(bi.size != nelem) {
            char buf[256];
            auto n = std::sprintf(buf, "bi size: %u. nelem: %u\n", int(bi.size), int(nelem));
            throw std::invalid_argument(std::string(buf, buf + n));
        }
        if(usefloat) {
            blaze::CustomVector<float, blz::unaligned, blz::unpadded> cv((float *)ptr, nelem);
            wrap.perform([&](const auto &x) {
                if(byrow) cv = blz::var<blz::rowwise>(x);
                else      cv = trans(blz::var<blz::columnwise>(x));
            });
        } else {
            blaze::CustomVector<double, blz::unaligned, blz::unpadded> cv((double *)ptr, nelem);
            wrap.perform([&](const auto &x) {
                if(byrow) cv = blz::var<blz::rowwise>(x);
                else      cv = trans(blz::var<blz::columnwise>(x));
            });
        }
        return ret;
    }, py::arg("kind")=-1, py::arg("usefloat")=true)
    .def("sum", [](SparseMatrixWrapper &wrap, int byrow, bool usefloat) -> py::object
    {
        switch(byrow) {case -1: case 0: case 1: break; default: throw std::invalid_argument("byrow must be -1 (total sum), 0 (by column) or by row (1)");}
        if(byrow == -1) {
            double ret;
            wrap.perform([&ret](const auto &x) {ret = blaze::sum(x);});
            return py::float_(ret);
        }
        py::array ret;
        Py_ssize_t nelem = byrow ? wrap.rows(): wrap.columns();
        if(usefloat) ret = py::array_t<float>(nelem);
                else ret = py::array_t<double>(nelem);
        auto bi = ret.request();
        auto ptr = bi.ptr;
        if(bi.size != nelem) {
            char buf[256];
            auto n = std::sprintf(buf, "bi size: %u. nelem: %u\n", int(bi.size), int(nelem));
            throw std::invalid_argument(std::string(buf, buf + n));
        }
        if(usefloat) {
            blaze::CustomVector<float, blz::unaligned, blz::unpadded> cv((float *)ptr, nelem);
            wrap.perform([&](const auto &x) {
                if(byrow) cv = blz::sum<blz::rowwise>(x);
                else      cv = trans(blz::sum<blz::columnwise>(x));
            });
        } else {
            blaze::CustomVector<double, blz::unaligned, blz::unpadded> cv((double *)ptr, nelem);
            wrap.perform([&](const auto &x) {
                if(byrow) cv = blz::sum<blz::rowwise>(x);
                else      cv = trans(blz::sum<blz::columnwise>(x));
            });
        }
        return ret;
    }, py::arg("kind")=-1, py::arg("usefloat")=true)
    .def("count_nnz", [](SparseMatrixWrapper &wrap, int byrow) -> py::object
    {
        if(byrow <= 0)
            return py::int_(wrap.nnz());
        const char *dt;
        if(wrap.columns() <= 0xFFu) {
            dt = "B";
        } else if(wrap.columns() <= 0xFFFFu) {
            dt = "H";
        } else if(wrap.columns() <= 0xFFFFFFFFu) {
            dt = "I";
        } else {
            dt = "L";
        }
        Py_ssize_t nr = wrap.rows();
        py::array ret(py::dtype(dt), std::vector<Py_ssize_t>({nr}));
        auto ptr = ret.request().ptr;
        switch(dt[0]) {
#define DTCASE(chr, type) case chr: {auto view = blz::make_cv((type *)ptr, nr); view = blaze::generate(nr, [&wrap](auto rowid) {return type(wrap.is_float() ? nonZeros(row(wrap.getfloat(), rowid, unchecked)): nonZeros(row(wrap.getdouble(), rowid, unchecked)));});} break
            DTCASE('L', uint64_t); DTCASE('I', uint32_t); DTCASE('H', uint16_t); DTCASE('B', uint8_t);
#undef DTCASE
            default: throw std::runtime_error("Unexpected dtype");
        }
        return ret;
    }, py::arg("byrow")=0);
#endif


    // Utilities
    m.def("valid_measures", []() {
        py::array_t<uint32_t> ret(sizeof(dist::USABLE_MEASURES) / sizeof(dist::USABLE_MEASURES[0]));
        std::transform(std::begin(dist::USABLE_MEASURES), std::end(dist::USABLE_MEASURES), (uint32_t *)ret.request().ptr, [](auto x) {return static_cast<uint32_t>(x);});
        return ret;
    });
    m.def("meas2desc", [](int x) -> std::string {
        return dist::prob2desc((dist::DissimilarityMeasure)x);
    });
    m.def("meas2str", [](int x) -> std::string {
        return dist::prob2str((dist::DissimilarityMeasure)x);
    });
    m.def("display_measures", [](){
        for(const auto _m: dist::USABLE_MEASURES) {
            std::fprintf(stderr, "%d\t%s\t%s\n", static_cast<int>(_m), prob2str(_m), prob2desc(_m));
        }
    });
    m.def("mdict", []() {
        py::dict ret;
        for(const auto d: dist::USABLE_MEASURES) {
            ret[dist::prob2str(d)] = static_cast<Py_ssize_t>(d);
        }
        return ret;
    });

    // SumOpts
    // Used for providing a pythonic interface for summary options
    py::class_<SumOpts>(m, "SumOpts")
    .def(py::init<std::string, Py_ssize_t, double, std::string, double, Py_ssize_t, bool, size_t>(), py::arg("measure"), py::arg("k") = 10, py::arg("prior") = 0., py::arg("sm") = "BFL", py::arg("outlier_fraction")=0., py::arg("max_rounds") = 100, py::arg("kmc2n") = 0,
        py::arg("soft") = false, "Construct a SumOpts object using a string key for the measure name and a string key for the coreest construction format.")
    .def(py::init<int, Py_ssize_t, double, std::string, double, Py_ssize_t, bool, size_t>(), py::arg("measure") = 0, py::arg("k") = 10, py::arg("prior") = 0., py::arg("sm") = "BFL", py::arg("outlier_fraction")=0., py::arg("max_rounds") = 100, py::arg("kmc2n") = 0,
        py::arg("soft") = false, "Construct a SumOpts object using a integer key for the measure name and a string key for the coreest construction format.")
    .def(py::init<std::string, Py_ssize_t, double, int, double, Py_ssize_t, bool, size_t>(), py::arg("measure") = "L1", py::arg("k") = 10, py::arg("prior") = 0., py::arg("sm") = static_cast<int>(minicore::coresets::BFL), py::arg("outlier_fraction")=0., py::arg("max_rounds") = 100, py::arg("kmc2n") = 0,
        py::arg("soft") = false, "Construct a SumOpts object using a string key for the measure name and an integer key for the coreest construction format.")
    .def(py::init<int, Py_ssize_t, double, int, double, Py_ssize_t, bool, size_t>(), py::arg("measure") = 0, py::arg("k") = 10, py::arg("prior") = 0., py::arg("sm") = static_cast<int>(minicore::coresets::BFL), py::arg("outlier_fraction")=0., py::arg("max_rounds") = 100, py::arg("kmc2n") = 0,
        py::arg("soft") = false, "Construct a SumOpts object using a integer key for the measure name and an integer key for the coreest construction format.")
    .def("__str__", &SumOpts::to_string)
    .def("__repr__", [](const SumOpts &x) {
        std::string ret = x.to_string();
        char buf[32];
        std::sprintf(buf, "%p", (void *)&x);
        ret += std::string(". Address: ") + buf;
        return ret;
    })
    .def_readwrite("kmc2n", &SumOpts::kmc2_rounds)
    .def_readwrite("lspp", &SumOpts::lspp)
    .def_readwrite("gamma", &SumOpts::gamma).def_readwrite("k", &SumOpts::k)
    .def_readwrite("search_max_rounds", &SumOpts::lloyd_max_rounds).def_readwrite("extra_sample_rounds", &SumOpts::extra_sample_tries)
    .def_readwrite("soft", &SumOpts::soft)
    .def_readwrite("outlier_fraction", &SumOpts::outlier_fraction)
    .def_readwrite("discrete_metric_search", &SumOpts::discrete_metric_search)
    .def_readwrite("use_exponential_skips", &SumOpts::use_exponential_skips)
    .def_property("cs",
            [](SumOpts &obj) -> py::str {
                return std::string(coresets::sm2str(obj.sm));
            },
            [](SumOpts &obj, py::object item) {
                Py_ssize_t val;
                if(py::isinstance<py::str>(item)) {
                    val = minicore::coresets::str2sm(py::cast<std::string>(item));
                } else if(py::isinstance<py::int_>(item)) {
                    val = py::cast<Py_ssize_t>(item);
                } else throw std::invalid_argument("value must be str or int");
                obj.sm = (minicore::coresets::SensitivityMethod)val;
            }
        )
    .def_property("prior", [](SumOpts &obj) -> py::str {
        switch(obj.prior) {
            case dist::NONE: return "NONE";
            case dist::DIRICHLET: return "DIRICHLET";
            case dist::FEATURE_SPECIFIC_PRIOR: return "FSP";
            case dist::GAMMA_BETA: return "GAMMA";
            default: throw std::invalid_argument(std::string("Invalid prior: ") + std::to_string((int)obj.prior));
        }
    }, [](SumOpts &obj, py::object asn) -> void {
        if(asn.is_none()) {
            obj.prior = dist::NONE;
            return;
        }
        if(py::isinstance<py::str>(asn)) {
            const std::map<std::string, dist::Prior> map {
                {"NONE", dist::NONE},
                {"DIRICHLET", dist::DIRICHLET},
                {"GAMMA", dist::GAMMA_BETA},
                {"GAMMA_BETA", dist::GAMMA_BETA},
                {"FSP", dist::FEATURE_SPECIFIC_PRIOR},
                {"FEATURE_SPECIFIC_PRIOR", dist::FEATURE_SPECIFIC_PRIOR},
            };
            auto key = std::string(py::cast<py::str>(asn));
            for(auto &i: key) i = std::toupper(i);
            auto it = map.find(std::string(py::cast<py::str>(asn)));
            if(it == map.end())
                throw std::out_of_range("Prior must be NONE, FSP, FEATURE_SPECIFIC_PRIOR, GAMMA, or DIRICHLET");
            obj.prior = it->second;
        } else if(py::isinstance<py::int_>(asn)) {
            auto x = py::cast<Py_ssize_t>(asn);
            if(x > 3) throw std::out_of_range("x must be <= 3 if an integer, to represent various priors");
            obj.prior = (dist::Prior)x;
        }
    });
    static constexpr const char *kmeans_doc =
        "Computes a selecion of points from the matrix pointed to by smw, returning indexes for selected centers, along with assignments and costs for each point."
       "\nSet nkmc to > 0 to use kmc2 instead of full D2 sampling, which is faster but may yield a slightly lower-quality result.\n"
        "One can accelerate sampling via SIMD (default) or exponential skips via use_exponential_skips=True\n";
    m.def("kmeanspp", run_kmpp_noso
       , kmeans_doc,
       py::arg("smw"), py::arg("msr"), py::arg("k"), py::arg("prior") = 0., py::arg("seed") = 0, py::arg("nkmc") = 0, py::arg("ntimes") = 1,
       py::arg("lspp") = 0, py::arg("expskips") = false, py::arg("n_local_trials") = 1,
       py::arg("weights") = py::none()
    );
#if 1
    m.def("kmeanspp", [](const SparseMatrixWrapper &smw, const SumOpts &so, py::object weights) {
        return run_kmpp_noso(smw, py::int_(int(so.dis)), py::int_(int(so.k)),  so.gamma, so.seed, so.kmc2_rounds, std::max(int(so.extra_sample_tries) - 1, 0),
                       so.lspp, so.use_exponential_skips, so.n_local_trials, weights);
    },
        kmeans_doc,
       py::arg("smw"),
       py::arg("opts"),
       py::arg("weights") = py::none()
    );
    m.def("d2_select",  [](SparseMatrixWrapper &smw, const SumOpts &so, py::object weights) {
        std::vector<uint32_t> centers, asn;
        std::vector<double> dc;
        double *wptr = nullptr;
        float *fwptr = nullptr;
        if(py::isinstance<py::array>(weights)) {
            auto inf = py::cast<py::array>(weights).request();
            switch(inf.format.front()) {
                case 'd': wptr = (double *)inf.ptr; break;
                case 'f': fwptr = (float *)inf.ptr; break;
                default: throw std::invalid_argument("Wrong format weights");
            }
        }
        auto lhs = std::tie(centers, asn, dc);
        if(wptr) {
            smw.perform([&](auto &x) {lhs = minicore::m2d2(x, so, wptr);});
        } else {
            // if fwptr is unset, fwptr is unused because is null,
            // so this branch includes floating-point weights and non-existent weights
            smw.perform([&](auto &x) {lhs = minicore::m2d2(x, so, fwptr);});
        }
        py::array_t<uint64_t> ret(centers.size());
        py::array_t<uint32_t> retasn(smw.rows());
        py::array_t<double> costs(smw.rows());
        auto rpi = ret.request(), api = retasn.request(), cpi = costs.request();
        std::copy(centers.begin(), centers.end(), (uint64_t *)rpi.ptr);
        std::copy(dc.begin(), dc.end(), (double *)cpi.ptr);
        std::copy(asn.begin(), asn.end(), (uint32_t *)api.ptr);
        return py::make_tuple(ret, retasn, costs);
    }, "Computes a selecion of points from the matrix pointed to by smw, returning indexes for selected centers, along with assignments and costs for each point.",
       py::arg("smw"), py::arg("sumopts"), py::arg("weights") = py::none());
#endif
    m.def("d2_select",  [](py::array arr, const SumOpts &so) {
        auto bi = arr.request();
        if(bi.ndim != 2) throw std::invalid_argument("arr must have 2 dimensions");
        if(bi.format.size() != 1)
            throw std::invalid_argument("bi format must be basic");
        std::vector<uint32_t> centers, asn;
        std::vector<double> dc;
        switch(bi.format.front()) {
            case 'f': {
                blaze::CustomMatrix<float, blaze::unaligned, blaze::unpadded> cm((float *)bi.ptr, bi.shape[0], bi.shape[1], bi.strides[1]);
                std::tie(centers, asn, dc) = minicore::m2d2(cm, so);
            } break;
            case 'd': {
                blaze::CustomMatrix<double, blaze::unaligned, blaze::unpadded> cm((double *)bi.ptr, bi.shape[0], bi.shape[1], bi.strides[1]);
                std::tie(centers, asn, dc) = minicore::m2d2(cm, so);
            } break;
            default: throw std::invalid_argument("Not supported: non-double/float type");
        }
        py::array_t<uint32_t> ret(centers.size()), retasn(bi.shape[0]);
        py::array_t<double> costs(bi.shape[0]);
        auto rpi = ret.request(), api = retasn.request(), cpi = costs.request();
        std::copy(centers.begin(), centers.end(), (uint32_t *)rpi.ptr);
        std::copy(dc.begin(), dc.end(), (double *)cpi.ptr);
        std::copy(asn.begin(), asn.end(), (uint32_t *)api.ptr);
        return py::make_tuple(ret, retasn, costs);
    }, "Computes a selecion of points from the matrix pointed to by smw, returning indexes for selected centers, along with assignments and costs for each point.",
       py::arg("data"), py::arg("sumopts"));
#if 1
    m.def("greedy_select",  [](SparseMatrixWrapper &smw, const SumOpts &so) {
        std::vector<uint64_t> centers;
        std::vector<double> dret;
        if(smw.is_float()) {
            std::tie(centers, dret) = minicore::m2greedysel(smw.getfloat(), so);
        } else {
            std::tie(centers, dret) = minicore::m2greedysel(smw.getdouble(), so);
        }
        py::array_t<uint32_t> ret(centers.size());
        py::array_t<double> costs(smw.rows());
        auto rpi = ret.request(), cpi = costs.request();
        std::copy(centers.begin(), centers.end(), (uint32_t *)rpi.ptr);
        std::copy(dret.begin(), dret.end(), (double *)cpi.ptr);
        return py::make_tuple(ret, costs);
    }, "Computes a greedy selection of points from the matrix pointed to by smw, returning indexes and a vector of costs for each point. To allow for outliers, use the outlier_fraction parameter of Sumopts.",
       py::arg("smw"), py::arg("sumopts"));
#endif



    m.def("greedy_select",  [](py::array arr, const SumOpts &so) {
        std::vector<uint64_t> centers;
        std::vector<double> dret;
        auto bi = arr.request();
        if(bi.ndim != 2) throw std::invalid_argument("arr must have 2 dimensions");
        if(bi.format.size() != 1)
            throw std::invalid_argument("bi format must be basic");
        switch(bi.format.front()) {
            case 'f': {
                blaze::CustomMatrix<float, blaze::unaligned, blaze::unpadded> cm((float *)bi.ptr, bi.shape[0], bi.shape[1], bi.strides[1]);
                std::tie(centers, dret) = minicore::m2greedysel(cm, so);
            } break;
            case 'd': {
                blaze::CustomMatrix<double, blaze::unaligned, blaze::unpadded> cm((double *)bi.ptr, bi.shape[0], bi.shape[1], bi.strides[1]);
                std::tie(centers, dret) = minicore::m2greedysel(cm, so);
            } break;
            default: throw std::invalid_argument("Not supported: non-double/float type");
        }
        py::array_t<uint32_t> ret(centers.size());
        py::array_t<double> costs(bi.shape[0]);
        auto rpi = ret.request(), cpi = costs.request();
        std::copy(centers.begin(), centers.end(), (uint32_t *)rpi.ptr);
        std::copy(dret.begin(), dret.end(), (double *)cpi.ptr);
        return py::make_tuple(ret, costs);
    }, "Computes a greedy selection of points from the matrix pointed to by smw, returning indexes and a vector of costs for each point. To allow for outliers, use the outlier_fraction parameter of Sumopts.",
       py::arg("data"), py::arg("sumopts"));
    m.def("smat_from_blaze", [](py::object path) {
         return SparseMatrixWrapper(path.cast<std::string>());
    }, py::arg("path"));
    m.def("tocsr", [](const SparseMatrixWrapper &lhs) {
        py::array_t<py::ssize_t> indptr(lhs.rows() + 1);
        py::array_t<int32_t> indices(lhs.nnz());
        py::array_t<double> data(lhs.nnz());
        py::array_t<py::ssize_t> shape(2);
        auto shapep = (py::ssize_t *)shape.request().ptr;
        shapep[0] = lhs.rows(); shapep[1] = lhs.columns();
        auto ipp = (py::ssize_t *)indptr.request().ptr;
        auto ip = (int32_t *)indices.request().ptr;
        auto fp = (double *)data.request().ptr;
        ipp[0] = 0;
        lhs.perform([&](auto &mat) {
            for(size_t i = 0; i < mat.rows(); ++i) {
                auto r = row(mat, i);
                ipp[i + 1] = ipp[i] + nonZeros(r);
                for(const auto &pair: r) {
                    *ip++ = pair.index();
                    *fp++ = pair.value();
                }
            }
        });
        return py::make_tuple(py::make_tuple(data, indices, indptr), shape);
    });
} // init_smw
