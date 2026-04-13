#include "vtkSHYXClebschMapFilter.h"

#include <vtkCell.h>
#include <vtkCellType.h>
#include <vtkDataArray.h>
#include <vtkDataSet.h>
#include <vtkDoubleArray.h>
#include <vtkInformation.h>
#include <vtkInformationVector.h>
#include <vtkMath.h>
#include <vtkNew.h>
#include <vtkObjectFactory.h>
#include <vtkPointData.h>

#ifdef VESPA_USE_SMP
#include <vtkSMPThreadLocal.h>
#include <vtkSMPTools.h>
#endif

#ifdef VESPA_USE_MKL
#define EIGEN_USE_MKL_ALL
#include <Eigen/PardisoSupport>
#endif

#include <Eigen/Dense>
#include <Eigen/IterativeLinearSolvers>
#include <Eigen/Sparse>

#include <cstdlib>
#include <cmath>
#include <complex>
#include <fstream>
#include <functional>
#include <random>
#include <string>
#include <unordered_map>
#include <vector>

VTK_ABI_NAMESPACE_BEGIN

namespace
{
std::string ClebschLogPath()
{
#ifdef _WIN32
    const char* tmp = std::getenv("TEMP");
    return tmp ? (std::string(tmp) + "\\clebsch_matrix_log.txt") : "clebsch_matrix_log.txt";
#else
    const char* tmp = std::getenv("TMPDIR");
    return (tmp ? std::string(tmp) : "/tmp") + "/clebsch_matrix_log.txt";
#endif
}
void ClebschLog(const std::string& msg)
{
    std::ofstream f(ClebschLogPath(), std::ios::app);
    if (f)
    {
        f << msg << "\n";
        f.flush();
    }
}
}

using Complex = std::complex<double>;
using SparseMatrixC = Eigen::SparseMatrix<Complex>;
using TripletC = Eigen::Triplet<Complex>;
static const Complex I(0.0, 1.0);

struct CgSolveResult
{
    int iterations;
    double residual;
};

namespace
{
void SolveHermitianSystemManual(int num_rows, const SparseMatrixC& A, const Eigen::VectorXcd& b,
    Eigen::Ref<Eigen::VectorXcd> x, double tol, int max_iter, CgSolveResult& result,
    std::function<void(int iter, double residual)> iter_cb)
{
    Eigen::DiagonalPreconditioner<Complex> M(A);
    Eigen::VectorXcd r = b - A * x;
    Eigen::VectorXcd z = M.solve(r);
    Eigen::VectorXcd p = z;
    Complex rho = r.adjoint() * z;
    double r0_norm = (b.norm() > 1e-14) ? b.norm() : 1.0;

    for (result.iterations = 0; result.iterations < max_iter; ++result.iterations)
    {
        Eigen::VectorXcd Ap = A * p;
        Complex pAp = p.adjoint() * Ap;
        if (std::abs(pAp.real()) < 1e-30)
            break;
        Complex alpha = rho / pAp;
        x += alpha * p;
        r -= alpha * Ap;
        result.residual = r.norm();
        if (iter_cb)
            iter_cb(result.iterations, result.residual);
        if (result.residual < tol * r0_norm)
            break;
        z = M.solve(r);
        Complex rho_new = r.adjoint() * z;
        Complex beta = rho_new / rho;
        p = z + beta * p;
        rho = rho_new;
    }
}

CgSolveResult SolveHermitianSystem(int num_rows, const std::vector<TripletC>& A_triplets,
    const std::vector<Complex>& b, std::vector<Complex>& x, bool use_mkl,
    std::function<void(int iter, double residual)> iter_cb,
    std::function<void(const std::string&)> log_fn)
{
    CgSolveResult result = { 0, 0.0 };
    SparseMatrixC A(num_rows, num_rows);
    A.setFromTriplets(A_triplets.begin(), A_triplets.end());

    Eigen::VectorXcd eigen_b(num_rows);
    for (int i = 0; i < num_rows; ++i)
        eigen_b[i] = b[i];

    Eigen::Map<Eigen::VectorXcd> eigen_x(x.data(), num_rows);

    bool use_cg = true;
    const double direct_residual_tol = 1.0;
    auto accept_direct = [&](double res) {
        return res <= direct_residual_tol;
    };
#if defined(VESPA_USE_MKL) && defined(EIGEN_USE_MKL_ALL)
    if (use_mkl)
    {
        Eigen::PardisoLLT<SparseMatrixC, Eigen::Lower> solver_llt;
        solver_llt.compute(A);
        if (solver_llt.info() == Eigen::Success)
        {
            eigen_x = solver_llt.solve(eigen_b);
            result.residual = (eigen_b - A * eigen_x).norm();
            if (accept_direct(result.residual))
            {
                result.iterations = 0;
                use_cg = false;
            }
            else if (log_fn)
                log_fn("PardisoLLT residual too large (" + std::to_string(result.residual) +
                    "), trying PardisoLDLT");
        }
        else if (log_fn)
            log_fn("PardisoLLT failed, trying PardisoLDLT");
        if (use_cg)
        {
            Eigen::PardisoLDLT<SparseMatrixC, Eigen::Lower> solver_ldlt;
            solver_ldlt.compute(A);
            if (solver_ldlt.info() == Eigen::Success)
            {
                eigen_x = solver_ldlt.solve(eigen_b);
                result.residual = (eigen_b - A * eigen_x).norm();
                if (accept_direct(result.residual))
                {
                    result.iterations = 0;
                    use_cg = false;
                }
                else if (log_fn)
                    log_fn("PardisoLDLT residual too large (" + std::to_string(result.residual) +
                        "), trying PardisoLU");
            }
            else if (log_fn)
                log_fn("PardisoLDLT failed, trying PardisoLU");
        }
        if (use_cg)
        {
            Eigen::PardisoLU<SparseMatrixC> solver_lu;
            solver_lu.compute(A);
            if (solver_lu.info() == Eigen::Success)
            {
                eigen_x = solver_lu.solve(eigen_b);
                result.residual = (eigen_b - A * eigen_x).norm();
                if (accept_direct(result.residual))
                {
                    result.iterations = 0;
                    use_cg = false;
                }
                else if (log_fn)
                    log_fn("PardisoLU residual too large (" + std::to_string(result.residual) +
                        "), falling back to CG");
            }
            else if (log_fn)
                log_fn("PardisoLU failed, falling back to CG");
        }
    }
#endif
    if (use_cg)
    {
        if (iter_cb)
        {
            const double tol = 1e-6;
            const int max_iter = (num_rows > 10000) ? num_rows : 10000;
            SolveHermitianSystemManual(num_rows, A, eigen_b, eigen_x, tol, max_iter, result, iter_cb);
        }
        else
        {
            Eigen::ConjugateGradient<SparseMatrixC, Eigen::Lower | Eigen::Upper,
                Eigen::DiagonalPreconditioner<Complex>>
                cg;
            cg.compute(A);
            cg.setTolerance(1e-6);
            eigen_x = cg.solveWithGuess(eigen_b, eigen_x);
            result.iterations = static_cast<int>(cg.iterations());
            result.residual = (eigen_b - A * eigen_x).norm();
        }
    }
    return result;
}
}

vtkStandardNewMacro(vtkSHYXClebschMapFilter);

vtkSHYXClebschMapFilter::vtkSHYXClebschMapFilter()
{
    this->SetNumberOfInputPorts(1);
    this->SetNumberOfOutputPorts(1);
}

vtkSHYXClebschMapFilter::~vtkSHYXClebschMapFilter()
{
    this->SetVelocityArrayName(nullptr);
}

int vtkSHYXClebschMapFilter::FillInputPortInformation(int port, vtkInformation* info)
{
    if (port == 0)
    {
        info->Set(vtkAlgorithm::INPUT_REQUIRED_DATA_TYPE(), "vtkDataSet");
        return 1;
    }
    return 0;
}

void vtkSHYXClebschMapFilter::PrintSelf(ostream& os, vtkIndent indent)
{
    this->Superclass::PrintSelf(os, indent);
    os << indent << "UseMKL: " << (this->UseMKL ? "On" : "Off") << "\n";
    os << indent << "Hbar: " << this->Hbar << "\n";
    os << indent << "AutoHbar: " << (this->AutoHbar ? "On" : "Off") << "\n";
    os << indent << "DeltaT: " << this->DeltaT << "\n";
    os << indent << "MaxIterations: " << this->MaxIterations << "\n";
    os << indent << "OuterLoops: " << this->OuterLoops << "\n";
    os << indent << "RandomSeed: " << this->RandomSeed << "\n";
    os << indent << "VelocityArrayName: "
       << (this->VelocityArrayName ? this->VelocityArrayName : "Velocity") << "\n";
}

int vtkSHYXClebschMapFilter::RequestData(
    vtkInformation*, vtkInformationVector** inputVector, vtkInformationVector* outputVector)
{
    vtkDataSet* input = vtkDataSet::GetData(inputVector[0]);
    vtkDataSet* output = vtkDataSet::GetData(outputVector);
    output->ShallowCopy(input);

    const char* velName = (this->VelocityArrayName && this->VelocityArrayName[0])
        ? this->VelocityArrayName
        : "Velocity";
    vtkDataArray* velocityArray = input->GetPointData()->GetVectors(velName);
    if (!velocityArray)
    {
        vtkErrorMacro("Input must have a PointData vector array named '" << velName << "'.");
        return 0;
    }

    int numPoints = input->GetNumberOfPoints();
    vtkIdType numCells = input->GetNumberOfCells();

    std::ofstream(ClebschLogPath()).close();
    ClebschLog("[1] Start: numPoints=" + std::to_string(numPoints) +
               ", numCells=" + std::to_string(numCells));

    std::vector<Complex> psi(numPoints * 2);
#ifdef VESPA_USE_SMP
    vtkSMPTools::For(0, numPoints, [&](vtkIdType begin, vtkIdType end) {
        std::mt19937 gen(this->RandomSeed + static_cast<unsigned>(begin));
        std::normal_distribution<double> dist(0.0, 1.0);
        for (vtkIdType i = begin; i < end; ++i)
        {
            Complex p1(dist(gen), dist(gen));
            Complex p2(dist(gen), dist(gen));
            double norm = std::sqrt(std::norm(p1) + std::norm(p2));
            psi[2 * i] = p1 / norm;
            psi[2 * i + 1] = p2 / norm;
        }
    });
#else
    std::mt19937 gen(this->RandomSeed);
    std::normal_distribution<double> dist(0.0, 1.0);
    for (int i = 0; i < numPoints; ++i)
    {
        Complex p1(dist(gen), dist(gen));
        Complex p2(dist(gen), dist(gen));
        double norm = std::sqrt(std::norm(p1) + std::norm(p2));
        psi[2 * i] = p1 / norm;
        psi[2 * i + 1] = p2 / norm;
    }
#endif
    ClebschLog("[2] Psi init done");

    auto make_edge_key = [](int p1, int p2) -> uint64_t {
        uint64_t min_p = static_cast<uint64_t>(std::min(p1, p2));
        uint64_t max_p = static_cast<uint64_t>(std::max(p1, p2));
        return (min_p << 32) | max_p;
    };

    std::unordered_map<uint64_t, int> edge_to_index;
    std::vector<std::pair<int, int>> edges;
    std::vector<double> M_V(numPoints, 0.0);
    std::vector<double> w_ij;
    std::vector<double> eta0;

    double max_u = 0.0;
    double sum_edge_length = 0.0;

    for (vtkIdType c = 0; c < input->GetNumberOfCells(); ++c)
    {
        vtkCell* cell = input->GetCell(c);
        if (cell->GetCellType() != VTK_TETRA)
            continue;

        int pts[4];
        Eigen::Vector3d pos[4];
        for (int i = 0; i < 4; ++i)
        {
            pts[i] = cell->GetPointId(i);
            input->GetPoint(pts[i], pos[i].data());
        }

        Eigen::Matrix3d J;
        J.col(0) = pos[1] - pos[0];
        J.col(1) = pos[2] - pos[0];
        J.col(2) = pos[3] - pos[0];
        double volume = std::abs(J.determinant()) / 6.0;
        if (volume < 1e-12)
            continue;

        for (int i = 0; i < 4; ++i)
            M_V[pts[i]] += volume / 4.0;

        Eigen::Matrix3d J_inv = J.inverse();
        Eigen::Vector3d grad_phi[4];
        grad_phi[1] = J_inv.row(0);
        grad_phi[2] = J_inv.row(1);
        grad_phi[3] = J_inv.row(2);
        grad_phi[0] = -(grad_phi[1] + grad_phi[2] + grad_phi[3]);

        for (int i = 0; i < 3; ++i)
        {
            for (int j = i + 1; j < 4; ++j)
            {
                uint64_t key = make_edge_key(pts[i], pts[j]);
                double local_weight = -volume * grad_phi[i].dot(grad_phi[j]);

                if (edge_to_index.find(key) == edge_to_index.end())
                {
                    int e_idx = static_cast<int>(edges.size());
                    edge_to_index[key] = e_idx;
                    int v_min = std::min(pts[i], pts[j]);
                    int v_max = std::max(pts[i], pts[j]);
                    edges.push_back({ v_min, v_max });
                    w_ij.push_back(local_weight);

                    double v1[3], v2[3], p1[3], p2[3];
                    velocityArray->GetTuple(v_min, v1);
                    velocityArray->GetTuple(v_max, v2);
                    input->GetPoint(v_min, p1);
                    input->GetPoint(v_max, p2);

                    Eigen::Vector3d u_avg = 0.5 * (Eigen::Vector3d(v1) + Eigen::Vector3d(v2));
                    Eigen::Vector3d edge_vec = Eigen::Vector3d(p2) - Eigen::Vector3d(p1);
                    double current_eta0 = u_avg.dot(edge_vec);
                    eta0.push_back(current_eta0);

                    max_u = std::max(max_u, u_avg.norm());
                    sum_edge_length += edge_vec.norm();
                }
                else
                {
                    w_ij[edge_to_index[key]] += local_weight;
                }
            }
        }
    }
    ClebschLog("[3] DEC precompute done: numEdges=" + std::to_string(edges.size()));

#ifdef VESPA_USE_SMP
    vtkSMPTools::For(0, numPoints, [&](vtkIdType begin, vtkIdType end) {
        for (vtkIdType i = begin; i < end; ++i)
            if (M_V[i] < 1e-12)
                M_V[i] = 1e-12;
    });
#else
    for (int i = 0; i < numPoints; ++i)
    {
        if (M_V[i] < 1e-12)
            M_V[i] = 1e-12;
    }
#endif

    if (this->AutoHbar && !edges.empty())
    {
        double avg_dr = sum_edge_length / static_cast<double>(edges.size());
        this->Hbar = (2.0 * avg_dr * max_u) / vtkMath::Pi() * 1.2;
        if (this->Hbar < 1e-6)
            this->Hbar = 0.1;
        ClebschLog("[4] AutoHbar: Hbar=" + std::to_string(this->Hbar));
    }

    double epsilon = 1.0;
    for (int outer = 0; outer < this->OuterLoops; ++outer)
    {
        ClebschLog("[5] Outer " + std::to_string(outer + 1) + "/" +
                  std::to_string(this->OuterLoops) + " epsilon=" + std::to_string(epsilon));

        auto get_s = [&](int idx) -> Eigen::Vector3d {
            Complex p1 = psi[2 * idx], p2 = psi[2 * idx + 1];
            return Eigen::Vector3d(2.0 * std::real(std::conj(p1) * p2),
                2.0 * std::imag(std::conj(p1) * p2), std::norm(p1) - std::norm(p2));
        };

        for (int inner = 0; inner < this->MaxIterations; ++inner)
        {
            std::vector<TripletC> A_triplets;
            std::vector<Complex> b(numPoints * 2);

#ifdef VESPA_USE_SMP
            vtkSMPThreadLocal<std::vector<TripletC>> threadMassTriplets;
            vtkSMPThreadLocal<std::vector<TripletC>> threadEdgeTriplets;
            vtkSMPTools::For(0, numPoints, [&](vtkIdType begin, vtkIdType end) {
                auto& local = threadMassTriplets.Local();
                for (vtkIdType i = begin; i < end; ++i)
                {
                    local.push_back(TripletC(2 * i, 2 * i, M_V[i]));
                    local.push_back(TripletC(2 * i + 1, 2 * i + 1, M_V[i]));
                    b[2 * i] = M_V[i] * psi[2 * i];
                    b[2 * i + 1] = M_V[i] * psi[2 * i + 1];
                }
            });
            for (auto it = threadMassTriplets.begin(); it != threadMassTriplets.end(); ++it)
                A_triplets.insert(A_triplets.end(), it->begin(), it->end());

            double c1 = 0.5 * (1.0 - epsilon);
            double c2 = 0.5 * (1.0 + epsilon);
            vtkSMPTools::For(0, static_cast<vtkIdType>(edges.size()), [&](vtkIdType begin, vtkIdType end) {
                auto& local = threadEdgeTriplets.Local();
                for (vtkIdType e = begin; e < end; ++e)
                {
                    int i = edges[e].first;
                    int j = edges[e].second;
                    Eigen::Vector3d sij = (get_s(i) + get_s(j)).normalized();
                    Complex P2[2][2] = { { c2 + c1 * sij[2], c1 * Complex(sij[0], -sij[1]) },
                        { c1 * Complex(sij[0], sij[1]), c2 - c1 * sij[2] } };
                    Complex phase = std::exp(I * (eta0[e] / this->Hbar));
                    Complex phase_conj = std::conj(phase);
                    double weight = w_ij[e] * this->DeltaT;
                    int r_i[2] = { 2 * i, 2 * i + 1 }, r_j[2] = { 2 * j, 2 * j + 1 };
                    for (int row = 0; row < 2; ++row)
                        for (int col = 0; col < 2; ++col)
                        {
                            Complex val = weight * P2[row][col];
                            local.push_back(TripletC(r_i[row], r_i[col], val));
                            local.push_back(TripletC(r_j[row], r_j[col], val));
                            local.push_back(TripletC(r_i[row], r_j[col], -val * phase_conj));
                            local.push_back(TripletC(r_j[row], r_i[col], -val * phase));
                        }
                }
            });
            for (auto it = threadEdgeTriplets.begin(); it != threadEdgeTriplets.end(); ++it)
                A_triplets.insert(A_triplets.end(), it->begin(), it->end());
#else
            for (int i = 0; i < numPoints; ++i)
            {
                A_triplets.push_back(TripletC(2 * i, 2 * i, M_V[i]));
                A_triplets.push_back(TripletC(2 * i + 1, 2 * i + 1, M_V[i]));
                b[2 * i] = M_V[i] * psi[2 * i];
                b[2 * i + 1] = M_V[i] * psi[2 * i + 1];
            }
            double c1 = 0.5 * (1.0 - epsilon);
            double c2 = 0.5 * (1.0 + epsilon);
            for (size_t e = 0; e < edges.size(); ++e)
            {
                int i = edges[e].first;
                int j = edges[e].second;
                Eigen::Vector3d sij = (get_s(i) + get_s(j)).normalized();
                Complex P2[2][2] = { { c2 + c1 * sij[2], c1 * Complex(sij[0], -sij[1]) },
                    { c1 * Complex(sij[0], sij[1]), c2 - c1 * sij[2] } };
                Complex phase = std::exp(I * (eta0[e] / this->Hbar));
                Complex phase_conj = std::conj(phase);
                double weight = w_ij[e] * this->DeltaT;
                int r_i[2] = { 2 * i, 2 * i + 1 }, r_j[2] = { 2 * j, 2 * j + 1 };
                for (int row = 0; row < 2; ++row)
                    for (int col = 0; col < 2; ++col)
                    {
                        Complex val = weight * P2[row][col];
                        A_triplets.push_back(TripletC(r_i[row], r_i[col], val));
                        A_triplets.push_back(TripletC(r_j[row], r_j[col], val));
                        A_triplets.push_back(TripletC(r_i[row], r_j[col], -val * phase_conj));
                        A_triplets.push_back(TripletC(r_j[row], r_i[col], -val * phase));
                    }
            }
#endif

            if (outer == 0 && inner == 0)
            {
                ClebschLog("[6] Matrix scale: " + std::to_string(2 * numPoints) + "x" +
                          std::to_string(2 * numPoints) + " nnz=" +
                          std::to_string(A_triplets.size()));
            }

            std::string innerTag = " outer=" + std::to_string(outer) + " inner=" +
                std::to_string(inner);
            ClebschLog("[7]" + innerTag + " assembly done, solving...");
            auto cgIterCb = [&innerTag](int iter, double residual) {
                ClebschLog("  CG iter=" + std::to_string(iter) +
                    " residual=" + std::to_string(residual));
            };
            auto logFn = [&innerTag](const std::string& msg) {
                ClebschLog(innerTag + " " + msg);
            };
            CgSolveResult cgResult = SolveHermitianSystem(
                numPoints * 2, A_triplets, b, psi, this->UseMKL, cgIterCb, logFn);
            ClebschLog("[8]" + innerTag + " solve done: CG_iters=" +
                std::to_string(cgResult.iterations) + " residual=" +
                std::to_string(cgResult.residual));

#ifdef VESPA_USE_SMP
            vtkSMPTools::For(0, numPoints, [&](vtkIdType begin, vtkIdType end) {
                for (vtkIdType i = begin; i < end; ++i)
                {
                    double norm = std::sqrt(std::norm(psi[2 * i]) + std::norm(psi[2 * i + 1]));
                    if (norm > 1e-12)
                    {
                        psi[2 * i] /= norm;
                        psi[2 * i + 1] /= norm;
                    }
                }
            });
#else
            for (int i = 0; i < numPoints; ++i)
            {
                double norm = std::sqrt(std::norm(psi[2 * i]) + std::norm(psi[2 * i + 1]));
                if (norm > 1e-12)
                {
                    psi[2 * i] /= norm;
                    psi[2 * i + 1] /= norm;
                }
            }
#endif
        }

        epsilon /= 10.0;
    }
    ClebschLog("[9] All outer loops done");

    vtkNew<vtkDoubleArray> psiArray;
    psiArray->SetName("Clebsch_Psi_S3");
    psiArray->SetNumberOfComponents(4);
    psiArray->SetNumberOfTuples(numPoints);

#ifdef VESPA_USE_SMP
    vtkSMPTools::For(0, numPoints, [&](vtkIdType begin, vtkIdType end) {
        for (vtkIdType i = begin; i < end; ++i)
        {
            psiArray->SetTuple4(i, std::real(psi[2 * i]), std::imag(psi[2 * i]),
                std::real(psi[2 * i + 1]), std::imag(psi[2 * i + 1]));
        }
    });
#else
    for (int i = 0; i < numPoints; ++i)
    {
        psiArray->SetTuple4(i, std::real(psi[2 * i]), std::imag(psi[2 * i]),
            std::real(psi[2 * i + 1]), std::imag(psi[2 * i + 1]));
    }
#endif
    output->GetPointData()->AddArray(psiArray);
    ClebschLog("[10] Output done");

    return 1;
}

VTK_ABI_NAMESPACE_END
