#include "vtkSHYXTetMeshRegionPartition.h"

#include <vtkCell.h>
#include <vtkCellData.h>
#include <vtkDataObject.h>
#include <vtkIdList.h>
#include <vtkInformation.h>
#include <vtkInformationVector.h>
#include <vtkIntArray.h>
#include <vtkMath.h>
#include <vtkNew.h>
#include <vtkObjectFactory.h>
#include <vtkPointData.h>
#include <vtkPoints.h>
#include <vtkUnstructuredGrid.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <numeric>
#include <queue>
#include <unordered_map>
#include <unordered_set>
#include <vector>

VTK_ABI_NAMESPACE_BEGIN

vtkStandardNewMacro(vtkSHYXTetMeshRegionPartition);

namespace
{
constexpr double kEps = 1e-12;

// ---------------------------------------------------------------------------
// Dual-graph adjacency: node -> list of (neighbor node, shared-face capacity).
using DualAdjacency = std::vector<std::vector<std::pair<int, double>>>;

// ---------------------------------------------------------------------------
// Hash for an unordered triangle face key (3 sorted point ids).
struct FaceKey
{
    vtkIdType a, b, c;
    bool operator==(const FaceKey& o) const { return a == o.a && b == o.b && c == o.c; }
};

struct FaceKeyHash
{
    std::size_t operator()(const FaceKey& k) const
    {
        std::uint64_t h = 1469598103934665603ull;
        auto mix = [&h](std::uint64_t v) {
            h ^= v;
            h *= 1099511628211ull;
        };
        mix(static_cast<std::uint64_t>(k.a));
        mix(static_cast<std::uint64_t>(k.b));
        mix(static_cast<std::uint64_t>(k.c));
        return static_cast<std::size_t>(h);
    }
};

FaceKey makeFaceKey(vtkIdType p0, vtkIdType p1, vtkIdType p2)
{
    vtkIdType v[3] = { p0, p1, p2 };
    std::sort(v, v + 3);
    return FaceKey{ v[0], v[1], v[2] };
}

double triangleArea(double* p0, double* p1, double* p2)
{
    double e1[3], e2[3], cr[3];
    vtkMath::Subtract(p1, p0, e1);
    vtkMath::Subtract(p2, p0, e2);
    vtkMath::Cross(e1, e2, cr);
    return 0.5 * vtkMath::Norm(cr);
}

// ---------------------------------------------------------------------------
// Dinic max-flow / min-cut on a small local graph.
class Dinic
{
public:
    explicit Dinic(int n)
        : Graph(n)
        , Level(n)
        , Iter(n)
    {
    }

    void AddEdge(int from, int to, double capForward, double capBackward)
    {
        Graph[from].push_back({ to, capForward, static_cast<int>(Graph[to].size()) });
        Graph[to].push_back({ from, capBackward, static_cast<int>(Graph[from].size()) - 1 });
    }

    double MaxFlow(int s, int t)
    {
        double flow = 0.0;
        while (this->Bfs(s, t))
        {
            std::fill(Iter.begin(), Iter.end(), 0);
            double f;
            while ((f = this->Dfs(s, t, std::numeric_limits<double>::max())) > kEps)
            {
                flow += f;
            }
        }
        return flow;
    }

    // Nodes reachable from s in the residual graph = source side of the min cut.
    std::vector<char> SourceSide(int s)
    {
        std::vector<char> vis(Graph.size(), 0);
        std::queue<int> q;
        q.push(s);
        vis[s] = 1;
        while (!q.empty())
        {
            int v = q.front();
            q.pop();
            for (const Edge& e : Graph[v])
            {
                if (e.cap > kEps && !vis[e.to])
                {
                    vis[e.to] = 1;
                    q.push(e.to);
                }
            }
        }
        return vis;
    }

private:
    struct Edge
    {
        int to;
        double cap;
        int rev;
    };

    bool Bfs(int s, int t)
    {
        std::fill(Level.begin(), Level.end(), -1);
        std::queue<int> q;
        Level[s] = 0;
        q.push(s);
        while (!q.empty())
        {
            int v = q.front();
            q.pop();
            for (const Edge& e : Graph[v])
            {
                if (e.cap > kEps && Level[e.to] < 0)
                {
                    Level[e.to] = Level[v] + 1;
                    q.push(e.to);
                }
            }
        }
        return Level[t] >= 0;
    }

    double Dfs(int v, int t, double f)
    {
        if (v == t)
        {
            return f;
        }
        for (int& i = Iter[v]; i < static_cast<int>(Graph[v].size()); ++i)
        {
            Edge& e = Graph[v][i];
            if (e.cap > kEps && Level[v] < Level[e.to])
            {
                double d = this->Dfs(e.to, t, std::min(f, e.cap));
                if (d > kEps)
                {
                    e.cap -= d;
                    Graph[e.to][e.rev].cap += d;
                    return d;
                }
            }
        }
        return 0.0;
    }

    std::vector<std::vector<Edge>> Graph;
    std::vector<int> Level;
    std::vector<int> Iter;
};

// ---------------------------------------------------------------------------
// BFS hop distances over the local (unweighted) adjacency, from a seed.
void bfsDistances(int seed, const std::vector<std::vector<int>>& localAdj, std::vector<int>& dist)
{
    std::fill(dist.begin(), dist.end(), -1);
    std::queue<int> q;
    dist[seed] = 0;
    q.push(seed);
    while (!q.empty())
    {
        int v = q.front();
        q.pop();
        for (int nb : localAdj[v])
        {
            if (dist[nb] < 0)
            {
                dist[nb] = dist[v] + 1;
                q.push(nb);
            }
        }
    }
}

int farthestNode(int seed, const std::vector<std::vector<int>>& localAdj)
{
    std::vector<int> dist(localAdj.size());
    bfsDistances(seed, localAdj, dist);
    int best = seed;
    int bestDist = 0;
    for (int i = 0; i < static_cast<int>(dist.size()); ++i)
    {
        if (dist[i] > bestDist)
        {
            bestDist = dist[i];
            best = i;
        }
    }
    return best;
}

// ---------------------------------------------------------------------------
// Split node set 'sub' (global indices) into two balanced parts via min-cut.
// 'side' is filled (length = global node count) with 0/1 for the nodes in sub.
// Returns false if the set cannot be split (fewer than 2 nodes).
bool bisect(const std::vector<int>& sub, const DualAdjacency& adj, double band,
    std::vector<char>& side)
{
    const int n = static_cast<int>(sub.size());
    if (n < 2)
    {
        return false;
    }

    std::unordered_map<int, int> g2l;
    g2l.reserve(n * 2);
    for (int i = 0; i < n; ++i)
    {
        g2l[sub[i]] = i;
    }

    // Local adjacency (both unweighted neighbor lists and weighted edges).
    std::vector<std::vector<int>> localAdj(n);
    std::vector<std::array<double, 3>> edges; // (u, v, capacity) with u < v
    for (int i = 0; i < n; ++i)
    {
        for (const auto& nbCap : adj[sub[i]])
        {
            auto it = g2l.find(nbCap.first);
            if (it == g2l.end())
            {
                continue; // neighbor outside this subset
            }
            int j = it->second;
            localAdj[i].push_back(j);
            if (i < j)
            {
                edges.push_back({ static_cast<double>(i), static_cast<double>(j), nbCap.second });
            }
        }
    }

    // Graph-diameter endpoints via double BFS sweep.
    int a = farthestNode(0, localAdj);
    int b = farthestNode(a, localAdj);
    if (a == b)
    {
        b = (a + 1) % n; // degenerate (e.g. no edges); pick any other node
    }

    std::vector<int> distA(n), distB(n);
    bfsDistances(a, localAdj, distA);
    bfsDistances(b, localAdj, distB);

    // Order nodes by (distA - distB): negative => closer to a, positive => closer to b.
    // Disconnected nodes (dist < 0) are pushed toward the nearer-by-construction terminal.
    std::vector<std::pair<double, int>> order(n);
    for (int i = 0; i < n; ++i)
    {
        double da = (distA[i] < 0) ? 1e9 : distA[i];
        double db = (distB[i] < 0) ? 1e9 : distB[i];
        order[i] = { da - db, i };
    }
    std::sort(order.begin(), order.end());

    int anchorCount = static_cast<int>(std::floor(n * (1.0 - band) * 0.5));
    anchorCount = std::max(1, std::min(anchorCount, (n - 1) / 2));

    std::vector<char> anchor(n, 0); // 1 = source anchor, 2 = sink anchor
    for (int i = 0; i < anchorCount; ++i)
    {
        anchor[order[i].second] = 1;
        anchor[order[n - 1 - i].second] = 2;
    }

    // Build flow network: locals 0..n-1, source = n, sink = n+1.
    double totalCap = 0.0;
    for (const auto& e : edges)
    {
        totalCap += e[2];
    }
    const double inf = totalCap + static_cast<double>(n) + 1.0;

    const int src = n;
    const int snk = n + 1;
    Dinic dinic(n + 2);
    for (const auto& e : edges)
    {
        int u = static_cast<int>(e[0]);
        int v = static_cast<int>(e[1]);
        dinic.AddEdge(u, v, e[2], e[2]); // undirected shared face
    }
    for (int i = 0; i < n; ++i)
    {
        if (anchor[i] == 1)
        {
            dinic.AddEdge(src, i, inf, 0.0);
        }
        else if (anchor[i] == 2)
        {
            dinic.AddEdge(i, snk, inf, 0.0);
        }
    }

    dinic.MaxFlow(src, snk);
    std::vector<char> srcSide = dinic.SourceSide(src);

    int cntA = 0;
    int cntB = 0;
    for (int i = 0; i < n; ++i)
    {
        if (srcSide[i])
        {
            ++cntA;
        }
        else
        {
            ++cntB;
        }
    }

    // Fallback: if the cut failed to separate (one side empty), split on the
    // (distA - distB) median ordering, which is always balanced.
    if (cntA == 0 || cntB == 0)
    {
        for (int i = 0; i < n; ++i)
        {
            int localNode = order[i].second;
            side[sub[localNode]] = (i < n / 2) ? 0 : 1;
        }
        return true;
    }

    for (int i = 0; i < n; ++i)
    {
        side[sub[i]] = srcSide[i] ? 0 : 1;
    }
    return true;
}

// ---------------------------------------------------------------------------
// Connected components of the sub-graph induced on the node subset 'sub'.
std::vector<std::vector<int>> inducedComponents(const std::vector<int>& sub, const DualAdjacency& adj)
{
    std::unordered_set<int> inSub(sub.begin(), sub.end());
    std::unordered_set<int> seen;
    seen.reserve(sub.size() * 2);
    std::vector<std::vector<int>> comps;
    for (int start : sub)
    {
        if (seen.count(start))
        {
            continue;
        }
        std::vector<int> members;
        std::queue<int> q;
        q.push(start);
        seen.insert(start);
        while (!q.empty())
        {
            int v = q.front();
            q.pop();
            members.push_back(v);
            for (const auto& nbCap : adj[v])
            {
                if (inSub.count(nbCap.first) && !seen.count(nbCap.first))
                {
                    seen.insert(nbCap.first);
                    q.push(nbCap.first);
                }
            }
        }
        comps.push_back(std::move(members));
    }
    return comps;
}

// ---------------------------------------------------------------------------
// Split a region budget 'k' across parts of the given sizes: every part gets at
// least one region, the rest is shared proportionally (largest remainder).
std::vector<int> distributeBudget(const std::vector<std::size_t>& sizes, int k)
{
    const int m = static_cast<int>(sizes.size());
    std::vector<int> kPer(m, 1);
    std::size_t total = std::accumulate(sizes.begin(), sizes.end(), std::size_t{ 0 });
    int remaining = k - m;
    if (remaining > 0 && total > 0)
    {
        std::vector<std::pair<double, int>> frac(m);
        int assigned = 0;
        for (int c = 0; c < m; ++c)
        {
            double share = static_cast<double>(remaining) * sizes[c] / total;
            int whole = static_cast<int>(std::floor(share));
            kPer[c] += whole;
            assigned += whole;
            frac[c] = { share - whole, c };
        }
        int leftover = remaining - assigned;
        std::sort(frac.begin(), frac.end(),
            [](const std::pair<double, int>& x, const std::pair<double, int>& y) {
                return x.first > y.first;
            });
        for (int i = 0; i < leftover && i < m; ++i)
        {
            kPer[frac[i].second] += 1;
        }
    }
    return kPer;
}

// ---------------------------------------------------------------------------
// Recursively decompose 'sub' into 'k' regions, writing labels into regionOf.
// A node set is always broken into its connected components first, so every
// emitted region is connected (a min cut only guarantees its source side is
// connected; the complement may be several islands).
void partitionRecursive(const std::vector<int>& sub, int k, const DualAdjacency& adj, double band,
    std::vector<int>& regionOf, int& nextLabel)
{
    if (sub.empty())
    {
        return;
    }

    std::vector<std::vector<int>> comps = inducedComponents(sub, adj);
    if (comps.size() > 1)
    {
        // Disconnected: each component is its own region (or more). Regions can
        // never span disconnected bodies, so the component count is a lower bound.
        std::vector<std::size_t> sizes;
        sizes.reserve(comps.size());
        for (const auto& c : comps)
        {
            sizes.push_back(c.size());
        }
        std::vector<int> kPer = distributeBudget(sizes, k);
        for (std::size_t c = 0; c < comps.size(); ++c)
        {
            partitionRecursive(comps[c], kPer[c], adj, band, regionOf, nextLabel);
        }
        return;
    }

    if (k <= 1 || sub.size() < 2)
    {
        int label = nextLabel++;
        for (int g : sub)
        {
            regionOf[g] = label;
        }
        return;
    }

    std::vector<char> side(regionOf.size(), 0);
    std::vector<int> partA;
    std::vector<int> partB;
    if (bisect(sub, adj, band, side))
    {
        partA.reserve(sub.size());
        partB.reserve(sub.size());
        for (int g : sub)
        {
            (side[g] == 0 ? partA : partB).push_back(g);
        }
    }

    if (partA.empty() || partB.empty())
    {
        int label = nextLabel++;
        for (int g : sub)
        {
            regionOf[g] = label;
        }
        return;
    }

    // Distribute the region budget proportionally to part size.
    int kA = static_cast<int>(std::llround(
        static_cast<double>(k) * partA.size() / static_cast<double>(sub.size())));
    kA = std::max(1, std::min(kA, k - 1));
    int kB = k - kA;

    partitionRecursive(partA, kA, adj, band, regionOf, nextLabel);
    partitionRecursive(partB, kB, adj, band, regionOf, nextLabel);
}

// ---------------------------------------------------------------------------
// Connected components of the dual graph (BFS).
std::vector<std::vector<int>> connectedComponents(const DualAdjacency& adj)
{
    const int n = static_cast<int>(adj.size());
    std::vector<int> comp(n, -1);
    std::vector<std::vector<int>> result;
    for (int s = 0; s < n; ++s)
    {
        if (comp[s] >= 0)
        {
            continue;
        }
        std::vector<int> members;
        std::queue<int> q;
        comp[s] = static_cast<int>(result.size());
        q.push(s);
        while (!q.empty())
        {
            int v = q.front();
            q.pop();
            members.push_back(v);
            for (const auto& nbCap : adj[v])
            {
                if (comp[nbCap.first] < 0)
                {
                    comp[nbCap.first] = comp[s];
                    q.push(nbCap.first);
                }
            }
        }
        result.push_back(std::move(members));
    }
    return result;
}

} // namespace

//------------------------------------------------------------------------------
vtkSHYXTetMeshRegionPartition::vtkSHYXTetMeshRegionPartition()
{
    this->SetNumberOfInputPorts(1);
    this->SetNumberOfOutputPorts(1);
}

//------------------------------------------------------------------------------
int vtkSHYXTetMeshRegionPartition::FillInputPortInformation(int port, vtkInformation* info)
{
    if (port == 0)
    {
        info->Set(vtkAlgorithm::INPUT_REQUIRED_DATA_TYPE(), "vtkUnstructuredGrid");
        return 1;
    }
    return 0;
}

//------------------------------------------------------------------------------
void vtkSHYXTetMeshRegionPartition::PrintSelf(ostream& os, vtkIndent indent)
{
    os << indent << "PartitionMethod: " << this->PartitionMethod << std::endl;
    os << indent << "NumberOfRegions: " << this->NumberOfRegions << std::endl;
    os << indent << "BalanceBand: " << this->BalanceBand << std::endl;
    os << indent << "UseFaceAreaWeights: " << (this->UseFaceAreaWeights ? "ON" : "OFF") << std::endl;
    this->Superclass::PrintSelf(os, indent);
}

//------------------------------------------------------------------------------
int vtkSHYXTetMeshRegionPartition::RequestData(
    vtkInformation* vtkNotUsed(request), vtkInformationVector** inputVector,
    vtkInformationVector* outputVector)
{
    vtkUnstructuredGrid* input = vtkUnstructuredGrid::GetData(inputVector[0]);
    vtkUnstructuredGrid* output = vtkUnstructuredGrid::GetData(outputVector);

    if (!input || !output)
    {
        vtkErrorMacro("Missing input or output.");
        return 0;
    }

    const vtkIdType numCells = input->GetNumberOfCells();
    if (input->GetNumberOfPoints() == 0 || numCells == 0)
    {
        vtkErrorMacro("Input mesh is empty.");
        return 0;
    }

    // Collect tetrahedral cells; map dual-graph node index -> original cell id.
    std::vector<vtkIdType> nodeToCell;
    for (vtkIdType ci = 0; ci < numCells; ++ci)
    {
        if (input->GetCellType(ci) == VTK_TETRA)
        {
            nodeToCell.push_back(ci);
        }
    }

    const int numNodes = static_cast<int>(nodeToCell.size());
    if (numNodes == 0)
    {
        vtkErrorMacro("Input contains no VTK_TETRA cells.");
        return 0;
    }

    vtkPoints* points = input->GetPoints();

    // Build the dual graph: tets sharing a triangular face are connected, with the
    // capacity set to the shared face area (or 1 when area weights are disabled).
    static const int faceVerts[4][3] = { { 0, 1, 2 }, { 0, 1, 3 }, { 0, 2, 3 }, { 1, 2, 3 } };

    DualAdjacency adj(numNodes);
    std::unordered_map<FaceKey, std::pair<int, double>, FaceKeyHash> faceMap;
    faceMap.reserve(numNodes * 4);

    vtkNew<vtkIdList> ptIds;
    for (int node = 0; node < numNodes; ++node)
    {
        input->GetCellPoints(nodeToCell[node], ptIds);
        if (ptIds->GetNumberOfIds() != 4)
        {
            continue;
        }
        vtkIdType p[4];
        for (int k = 0; k < 4; ++k)
        {
            p[k] = ptIds->GetId(k);
        }
        for (int f = 0; f < 4; ++f)
        {
            vtkIdType v0 = p[faceVerts[f][0]];
            vtkIdType v1 = p[faceVerts[f][1]];
            vtkIdType v2 = p[faceVerts[f][2]];
            FaceKey key = makeFaceKey(v0, v1, v2);

            double cap = 1.0;
            if (this->UseFaceAreaWeights)
            {
                double a[3], b[3], c[3];
                points->GetPoint(v0, a);
                points->GetPoint(v1, b);
                points->GetPoint(v2, c);
                cap = triangleArea(a, b, c);
                if (cap < kEps)
                {
                    cap = kEps;
                }
            }

            auto it = faceMap.find(key);
            if (it == faceMap.end())
            {
                faceMap.emplace(key, std::make_pair(node, cap));
            }
            else
            {
                int other = it->second.first;
                if (other >= 0 && other != node)
                {
                    adj[node].push_back({ other, cap });
                    adj[other].push_back({ node, cap });
                }
                it->second.first = -1; // face already matched (ignore further sharers)
            }
        }
    }

    // Assign region labels.
    std::vector<int> regionOf(numNodes, 0);
    std::vector<std::vector<int>> comps = connectedComponents(adj);
    const int numComps = static_cast<int>(comps.size());

    int producedRegions = 0;
    if (this->PartitionMethod == CONNECTED_COMPONENTS)
    {
        for (int c = 0; c < numComps; ++c)
        {
            for (int g : comps[c])
            {
                regionOf[g] = c;
            }
        }
        producedRegions = numComps;
    }
    else
    {
        // partitionRecursive itself splits each node set into connected components
        // before bisecting, so every emitted region is connected and the region
        // budget is shared across components (component count is the lower bound).
        std::vector<int> allNodes(numNodes);
        std::iota(allNodes.begin(), allNodes.end(), 0);

        int nextLabel = 0;
        partitionRecursive(allNodes, this->NumberOfRegions, adj, this->BalanceBand, regionOf,
            nextLabel);
        producedRegions = nextLabel;
    }

    // Build output: original mesh + RegionId cell/point arrays.
    output->ShallowCopy(input);

    vtkNew<vtkIntArray> cellRegion;
    cellRegion->SetName("RegionId");
    cellRegion->SetNumberOfComponents(1);
    cellRegion->SetNumberOfTuples(numCells);
    cellRegion->FillValue(-1);

    vtkNew<vtkIntArray> pointRegion;
    pointRegion->SetName("RegionId");
    pointRegion->SetNumberOfComponents(1);
    pointRegion->SetNumberOfTuples(input->GetNumberOfPoints());
    pointRegion->FillValue(-1);

    for (int node = 0; node < numNodes; ++node)
    {
        vtkIdType ci = nodeToCell[node];
        cellRegion->SetValue(ci, regionOf[node]);

        input->GetCellPoints(ci, ptIds);
        for (vtkIdType k = 0; k < ptIds->GetNumberOfIds(); ++k)
        {
            pointRegion->SetValue(ptIds->GetId(k), regionOf[node]);
        }
    }

    output->GetCellData()->AddArray(cellRegion);
    output->GetCellData()->SetActiveScalars("RegionId");
    output->GetPointData()->AddArray(pointRegion);

    vtkDebugMacro(<< "Decomposed " << numNodes << " tetrahedra into " << producedRegions
                  << " region(s) across " << numComps << " connected component(s).");

    return 1;
}

VTK_ABI_NAMESPACE_END
