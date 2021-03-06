#include <algorithm>
#include <cassert>
#include <cmath>
#include <iomanip>
#ifndef NDEBUG
#include <iostream>
#endif
#include <limits>
#include <set>
#include <stdexcept>
#include <vector>

#include "blockdecoder.h"
#include "ctrlmat.h"

#define EPSILON_FLIP 0.001

/*== debug utility functions =================================================*/

#ifndef NDEBUG
template<typename T>
static std::string sprint_word(char const *name, std::vector<T> const &vect)
{
    std::stringstream ss;
    ss << name << " = (";
    for (size_t i = 0u; i < vect.size(); ++i)
        ss << vect[i] << (i == vect.size() - 1 ? ')' : ' ');
    return ss.str();
}

template<typename T>
static std::string sprint_set(char const *name, std::set<T> const &set)
{
    std::stringstream ss;
    ss << name << " = {";
    for (T const &val : set)
        ss << val << ", ";
    std::string ret = ss.str();
    ret.replace(ret.size() - 2, 2, "}");
    return ret;
}

static std::string sprint_matrix(
    char const *name, std::vector<double> const &mat, int height, int width)
{
    static int const CELL_WIDTH = 7;

    std::stringstream ss, ss_double;
    ss_double << std::fixed << std::setprecision(2);

    ss << name << ":\n";
    for (int i = 0; i < height; ++i) {
        ss << '|';
        for (int j = 0; j < width; ++j) {
            double val = mat[i * width + j];

            // cell formatting
            if (std::isnan(val)) {
                ss << std::string(CELL_WIDTH, ' ') << '|';
            } else {
                ss_double << val;
                std::string str_double = ss_double.str();
                ss_double.str("");

                // padding
                for (auto k = 0u; k < CELL_WIDTH - str_double.size(); ++k)
                    ss << ' ';

                ss << str_double << "|";
            }
        }
        ss << '\n';
    }
    return ss.str();
}
#endif


/*== bit flipping algorithms =================================================*/

template<typename E>
static bool bf_decode(CtrlMat const &H, int max_iter, double alpha,
    std::vector<double> const &in, std::vector<int> &out,
    bool weighted = false, bool modified = false, bool improved = false)
{
    assert(!modified || weighted);
    assert(!improved || (modified && weighted));

#ifndef NDEBUG
    if (modified)
        std::cout << "DECODING (MWBF):\n";
    else if (weighted)
        std::cout << "DECODING (WBF):\n";
    else
        std::cout << "DECODING (BF):\n";

    std::cout << sprint_word("b", in) << '\n';
#endif

    size_t w_size = improved ? H.k * H.n : (weighted ? H.k : 0);
    std::vector<double> w(w_size, std::numeric_limits<double>::max());
    std::vector<int> s(H.k, 0);
    std::vector<E> e(H.n, 0);

    for (int j = 0; j < H.n; ++j)
        out[j] = in[j] < 0.0 ? 1 : 0;
#ifndef NDEBUG
    std::cout << sprint_word("b_h", out) << (weighted ? "\n" : "\n\n");
#endif

    for (int iter = 0; iter < max_iter; ++iter) {

        bool is_codeword = true;
        for(int i = 0; i < H.k; ++i) {
            s[i] = 0;
            for (int j : H.K[i]) {
                s[i] ^= out[j];

                if (iter == 0 && weighted && !improved)
                    w[i] = std::min(w[i], std::abs(in[j]));
            }

            if (s[i] == 1)
                is_codeword = false;
        }
#ifndef NDEBUG
        if (weighted && iter == 0)
            std::cout << sprint_word("w", w) << "\n\n";

        std::cout << "=== " << iter + 1 << ". iteration ===\n";
        std::cout << sprint_word("s", s) << '\n';
#endif

        if (is_codeword) {
#ifndef NDEBUG
            std::cout << " => codeword\n";
#endif
            return true;
        }
#ifndef NDEBUG
        std::cout << " => no codeword\n";
#endif

        for (int j = 0; j < H.n; ++j) {
            if (modified)
                e[j] = -alpha * std::abs(in[j]);
            else
                e[j] = 0;

            for (int i : H.N[j]) {

                if (improved) {
                    if (iter == 0) {
                        for (int jp : H.K[i]) {
                            if (jp == j)
                                continue;

                            w[i * H.n + j] =
                                std::min(w[i * H.n + j], std::abs(in[jp]));
                        }
                    }
                }

                if (improved) {
                    e[j] += (2 * s[i] - 1) * w[i * H.n + j];
                } else if (modified) {
                    e[j] += (2 * s[i] - 1) * w[i];
                } else if (weighted)
                    e[j] += (2 * s[i] - 1) * w[i];
                else
                    e[j] += s[i];
            }
        }
#ifndef NDEBUG
        std::cout << sprint_word("e", e) << '\n';
#endif

        std::set<int> to_flip;
        E T_max = *std::max_element(e.begin(), e.end());

        for (int j = 0; j < H.n; ++j) {
            if (weighted) {
                if (std::abs(e[j] - T_max) < EPSILON_FLIP)
                    to_flip.insert(j);
            } else {
                if (e[j] == T_max)
                    to_flip.insert(j);
            }
        }
#ifndef NDEBUG
        std::cout << sprint_set("flip", to_flip) << '\n';
#endif

        for (int index : to_flip)
            out[index] ^= 1;
#ifndef NDEBUG
        std::cout << sprint_word(" => b_korr", out) << "\n\n";
#endif
    }

#ifndef NDEBUG
    std::cout << " => failure\n";
#endif
    return false;
}

bool BF::_decode(std::vector<double> const &in, std::vector<int> &out)
{
    return bf_decode<int>(H, max_iter, 0.0, in, out);
}

bool WBF::_decode(std::vector<double> const &in, std::vector<int> &out)
{
    return bf_decode<double>(H, max_iter, 0.0, in, out, true);
}

bool MWBF::_decode(std::vector<double> const &in, std::vector<int> &out)
{
    return bf_decode<double>(H, max_iter, alpha, in, out, true, true);
}

bool IMWBF::_decode(std::vector<double> const &in, std::vector<int> &out)
{
    return bf_decode<double>(H, max_iter, alpha, in, out, true, true, true);
}


/*== MLG variants ============================================================*/

bool OneStepMLG::_decode(std::vector<double> const &in, std::vector<int> &out)
{
#ifndef NDEBUG
    std::cout << "DECODING (one step MLG):\n";
    std::cout << sprint_word("b", in) << "\n";
#endif

    int gamma_half = H.N[0].size() / 2;

    std::vector<int> s(H.n);
    std::vector<int> e(H.n);

    for (int j = 0; j < H.n; ++j)
        out[j] = in[j] < 0.0 ? 1 : 0;
#ifndef NDEBUG
    std::cout << sprint_word("b_h", out) << "\n";
#endif

    bool is_codeword = true;
    for (int i = 0; i < H.n; ++i) {
        s[i] = 0;
        for (int j : H.K[i])
            s[i] ^= out[j];

        if (s[i] == 1)
            is_codeword = false;
    }
#ifndef NDEBUG
    std::cout << sprint_word("s", s) << "\n";
#endif

    if (is_codeword) {
#ifndef NDEBUG
        std::cout << " => codeword\n";
#endif
        return true;
    }
#ifndef NDEBUG
    std::cout << " => no codeword\n";
#endif

    for (int j = 0; j < H.n; ++j) {
        e[j] = 0;
        for (int i : H.N[j])
            e[j] += s[i];

        if (e[j] > gamma_half)
            out[j] ^= 1;
    }
#ifndef NDEBUG
    std::cout << sprint_word("e", e) << "\n";
    std::cout << sprint_word(" => b_korr", out) << "\n";
#endif

    return true;
}

template<typename R>
static bool iterative_mlg_decode(CtrlMat const &H, int max_iter, double alpha,
    std::vector<double> const &in, std::vector<int> &out, bool soft, bool adaptive)
{
#ifndef NDEBUG
    if (adaptive)
        std::cout << "DECODING (adaptive soft MLG):\n";
    else if (soft)
        std::cout << "DECODING (soft MLG):\n";
    else
        std::cout << "DECODING (hard MLG):\n";

    std::cout << sprint_word("b", in) << "\n";
#endif

    int gamma = H.N[0].size();

    int x = 3;
    R max = soft ? (1 << (x - 1)) - 1 : gamma;
    R min = -max;

    std::vector<R> r(H.n);
    std::vector<int> s(H.n);
    std::vector<int> e(H.n);

    // used only during adaptive soft MLG
    std::vector<int> w(H.k * H.n, std::numeric_limits<int>::max());

    for (int j = 0; j < H.n; ++j) {
        out[j] = in[j] < 0.0 ? 1 : 0;
        if (soft) {
            R q = std::round(in[j] * max);
            r[j] = std::min(std::max(q, min), max);
        } else {
            r[j] = out[j] ? min : max;
        }
    }
#ifndef NDEBUG
    std::cout << sprint_word("b_h", out) << "\n";
    std::cout << sprint_word("r", r) << "\n\n";
#endif

    if (adaptive) {
        for (int i = 0; i < H.k; ++i) {
            for (int j = 0; j < H.n; ++j) {
                R wij_min = std::numeric_limits<int>::max();

                for (int jp : H.K[i]) {
                    if (jp == j)
                        continue;

                    wij_min = std::min(wij_min, std::abs(r[jp]));
                }

                w[i * H.n + j] = min;
            }
        }
    }

    for (int iter = 0; iter < max_iter; ++iter) {
#ifndef NDEBUG
        std::cout << "=== " << iter + 1 << ". iteration ===\n";
#endif

        bool is_codeword = true;
        for (int i = 0; i < H.n; ++i) {
            s[i] = 0;
            for (int j : H.K[i])
                s[i] ^= out[j];
            if (s[i] == 1)
                is_codeword = false;
        }
#ifndef NDEBUG
        std::cout << sprint_word("s", s) << "\n";
#endif

        if (is_codeword) {
#ifndef NDEBUG
            std::cout << " => codeword\n";
#endif
            return true;
        }
#ifndef NDEBUG
        std::cout << " => no codeword\n";
#endif

        for (int j = 0; j < H.n; ++j) {
            e[j] = 0;
            for (int i : H.N[j]) {
                if (adaptive) {
                    e[j] += (2 * (s[i] ^ out[j]) - 1) * w[i * H.n + j];
                } else {
                    e[j] += 2 * (s[i] ^ out[j]) - 1;
                }
            }
        }
#ifndef NDEBUG
        std::cout << sprint_word("e", e) << "\n";
#endif

        for (int j = 0; j < H.n; ++j) {
            if (adaptive) {
                r[j] = std::min(std::max(
                    r[j] - alpha * e[j], (double) min), (double) max);
            } else {
                r[j] = std::min(std::max(r[j] - e[j], min), max);
            }
            out[j] = r[j] < 0 ? 1 : 0;
        }
#ifndef NDEBUG
        std::cout << sprint_word("r", r) << "\n";
        std::cout << sprint_word(" => b_korr", out) << "\n\n";
#endif
    }

#ifndef NDEBUG
    std::cout << " => failure\n";
#endif
    return false;
}

bool HardMLG::_decode(std::vector<double> const &in, std::vector<int> &out)
{
    return iterative_mlg_decode<int>(H, max_iter, 0.0, in, out, false, false);
}

bool SoftMLG::_decode(std::vector<double> const &in, std::vector<int> &out)
{
    return iterative_mlg_decode<int>(H, max_iter, 0.0, in, out, true, false);
}

bool AdaptiveSoftMLG::_decode(std::vector<double> const &in, std::vector<int> &out)
{
    return iterative_mlg_decode<double>(H, max_iter, alpha, in, out, true, true);
}


/*== Min Sum variants ========================================================*/

static bool hard_decide(std::vector<int> const &out, CtrlMat const &H)
{
    for (int i = 0; i < H.k; ++i) {
        int s = 0;
        for (int j : H.K[i])
            s ^= out[j];
         if (s == 1)
            return false;
    }
    return true;
}

static bool min_sum_decode(CtrlMat const &H, int max_iter, double alpha,
    std::vector<double> const &in, std::vector<int> &out, bool normalized, bool offset)
{
    if (normalized && offset)
        throw std::invalid_argument("normalized + offset min sum not supported");

    double double_max = std::numeric_limits<double>::max();

#ifndef NDEBUG
    std::cout << "DECODING (min sum):\n";
    std::cout << sprint_word("b", in) << '\n';
#endif

    int k = H.k;
    int n = H.n;

    std::vector<double> Q(k * n, std::nan(""));
    std::vector<double> R(k * n, std::nan(""));

    std::vector<double> min1(k, double_max);
    std::vector<double> min2(k, double_max);
    std::vector<int> sgn(k, 0); // 0 -> positive, 1 -> negative

    // Check if hard decision leads to codeword.
    for (int i = 0; i < n; ++i)
        out[i] = in[i] < 0.0;

#ifndef NDEBUG
    std::cout << sprint_word("b_h", out);
#endif

    if (hard_decide(out, H)) {
#ifndef NDEBUG
        std::cout << " => codeword\n\n";
#endif
        return true;
    }
#ifndef NDEBUG
    else {
        std::cout << " => no codeword\n\n";
    }
#endif

    for (int iter = 0; iter < max_iter; ++iter) {
#ifndef NDEBUG
        std::cout << "=== " << iter + 1 << ". iteration ===\n\n";
#endif

        // keep track of each rows two smallest entries and it's 'signum'.
        for (int i = 0; i < k; ++i) {
            min1[i] = double_max;
            min2[i] = double_max;
            sgn[i] = 0;

            for (int j : H.K[i]) {
                if (iter == 0)
                    Q[i * n + j] = in[j];

                double q = Q[i * n + j];
                double q_abs = std::abs(q);

                if (q_abs < min1[i]) {
                    min2[i] = min1[i];
                    min1[i] = q_abs;
                } else if (q_abs < min2[i]) {
                    min2[i] = q_abs;
                }

                if (q < 0.0)
                    sgn[i] ^= 1;
            }
        }
#ifndef NDEBUG
        std::cout << sprint_matrix("Q", Q, k, n) << "\n";
#endif

        // Update R using minima and signums just calculated.
        for (int i = 0; i < k; ++i) {
            for (int j : H.K[i]) {
                double q = Q[i * n + j];
                double q_abs = std::abs(q);
                double r = q_abs == min1[i] ? min2[i] : min1[i];

                if (normalized) {
                    R[i * n + j] = (1.0 / alpha) * (sgn[i] ^ (q < 0.0) ? -r : r);
                } else if (offset) {
                    double tmp = std::max(r - alpha, 0.0);
                    R[i * n + j] = sgn[i] ^ (q < 0.0) ? -tmp : tmp;
                } else {
                    R[i * n + j] = sgn[i] ^ (q < 0.0) ? -r : r;
                }
            }
        }
#ifndef NDEBUG
        std::cout << sprint_matrix("R", R, k, n) << "\n";
#endif

        // Calculate Extrinsic information and use it to construct new b and Q.
        for (int j = 0; j < n; ++j) {
            double Le = 0.0;
            for (int i : H.N[j])
                Le += R[i * n + j];

            out[j] = in[j] + Le < 0.0 ? 1 : 0;

            for (int i : H.N[j])
                Q[i * n + j] = in[j] + Le - R[i * n + j];
        }
#ifndef NDEBUG
        std::cout << sprint_word("b_korr", out);
#endif

        // Check if hard decision leads to codeword.
        if (hard_decide(out, H)) {
#ifndef NDEBUG
            std::cout << " => codeword\n\n";
#endif
            return true;
        }
#ifndef NDEBUG
        else {
            std::cout << " => no codeword\n\n";
        }
#endif

    }

#ifndef NDEBUG
    std::cout << " => failure\n\n";
#endif
    return false;
}

bool MinSum::_decode(std::vector<double> const &in, std::vector<int> &out)
{
    return min_sum_decode(H, max_iter, 0.0, in, out, false, false);
}

bool NormalizedMinSum::_decode(std::vector<double> const &in, std::vector<int> &out)
{
    return min_sum_decode(H, max_iter, alpha, in, out, true, false);
}

bool OffsetMinSum::_decode(std::vector<double> const &in, std::vector<int> &out)
{
    return min_sum_decode(H, max_iter, alpha, in, out, false, true);
}
