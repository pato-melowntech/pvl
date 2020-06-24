#pragma once

#include "PlyWriter.hpp"
#include "TriangleMesh.hpp"
#include <queue>

namespace Pvl {

template <typename Vec>
struct SimpleDecimator {
    template <typename Mesh>
    typename Vec::Float cost(const Mesh& mesh, const Graph::CollapseContext& context) const {
        Vec v1 = mesh.point(context.remaining);
        Vec v2 = mesh.point(context.removed);
        return normSqr(v1 - v2);
    }

    template <typename Mesh>
    Vec placement(const Mesh& mesh, const Graph::CollapseContext& context) const {
        Vec v1 = mesh.point(context.remaining);
        Vec v2 = mesh.point(context.removed);
        return 0.5 * (v1 + v2);
    }

    template <typename Mesh>
    void postprocess(const Mesh&, const Graph::CollapseContext&) const {}
};

class EdgeCountStop {
    std::size_t count_;

public:
    EdgeCountStop(std::size_t count)
        : count_(count) {}

    bool operator()(std::size_t collapsed) const {
        return collapsed > count_;
    }
};

class CollapseQueue {
    using EdgeCost = std::pair<float, EdgeHandle>;
    std::map<EdgeHandle, EdgeCost> edges_;
    std::set<EdgeCost> queue_;

public:
    void insert(EdgeHandle eh, const float cost) {
        EdgeCost ec{ cost, eh };
        PVL_ASSERT(edges_.find(eh) == edges_.end());
        queue_.insert(ec);
        edges_.insert(std::make_pair(eh, ec));
    }

    /*void update(EdgeHandle eh, const float cost) {
        PVL_ASSERT(edges_.find(eh) != edges_.end());
        EdgeCost oldEc = edges_[eh];
        queue_.erase(oldEc);

        PVL_ASSERT(oldEc.second == eh);
        EdgeCost newEc{ cost, eh };
        queue_.insert(newEc);
        edges_[eh] = newEc;
    }*/

    std::pair<EdgeHandle, float> pop() {
        EdgeCost ec = *queue_.begin();
        queue_.erase(ec);
        std::size_t erased = edges_.erase(ec.second);
        PVL_ASSERT(erased == 1);
        return std::make_pair(ec.second, ec.first);
    }

    void remove(EdgeHandle eh) {
        /// \todo
        if (edges_.find(eh) == edges_.end()) {
            return;
        }
        PVL_ASSERT(edges_.find(eh) != edges_.end());
        EdgeCost ec = edges_[eh];
        queue_.erase(ec);
        PVL_ASSERT(eh == ec.second);
        edges_.erase(eh);
    }

    bool empty() const {
        return queue_.empty();
    }
};

template <typename Vec, typename Index>
void savePatch(const TriangleMesh<Vec, Index>& mesh, HalfEdgeHandle eh) {
    TriangleMesh<Vec, Index> patch;
    std::set<FaceHandle> faces;
    for (FaceHandle fh : mesh.faceRing(mesh.from(eh))) {
        faces.insert(fh);
    }
    for (FaceHandle fh : mesh.faceRing(mesh.to(eh))) {
        faces.insert(fh);
    }

    std::set<VertexHandle> vertices;
    for (FaceHandle fh : faces) {
        VertexHandle v1 = patch.addVertex();
        VertexHandle v2 = patch.addVertex();
        VertexHandle v3 = patch.addVertex();
        patch.addFace(v1, v2, v3);

        for (VertexHandle vh : mesh.vertexRing(fh)) {
            vertices.insert(vh);
        }

        std::array<int, 3> is = mesh.faceIndices(fh);
        patch.points.push_back(mesh.points[is[0]]);
        patch.points.push_back(mesh.points[is[1]]);
        patch.points.push_back(mesh.points[is[2]]);
    }
    std::ofstream ofs("patch-faces.ply");
    PlyWriter writer(ofs);
    writer << patch;

    std::ofstream out("patch-points.ply");
    out << "ply\n";
    out << "format ascii 1.0\n";
    out << "comment Created by PVL library\n";
    out << "element vertex " << vertices.size() << "\n";
    out << "property float x\n";
    out << "property float y\n";
    out << "property float z\n";
    out << "property uchar red\n";
    out << "property uchar green\n";
    out << "property uchar blue\n";
    out << "element face 0\n";
    out << "property list uchar int vertex_index\n";
    out << "end_header\n";
    for (VertexHandle vh : vertices) {
        Vec p = mesh.points[vh];
        Vec c;
        if (vh == mesh.from(eh)) {
            c = Vec(0, 0, 255);
        } else if (vh == mesh.to(eh)) {
            c = Vec(255, 0, 0);
        } else {
            c = Vec(128, 128, 128);
        }
        out << p[0] << " " << p[1] << " " << p[2] << " " << c[0] << " " << c[1] << " " << c[2]
            << "\n";
    }
}

/// \todo add stop to decimator? (cost stop)
template <typename Vec, typename Index, typename Decimator, typename Stop>
void simplify(TriangleMesh<Vec, Index>& mesh, Decimator& decimator, const Stop& stop) {
    CollapseQueue queue;
    for (EdgeHandle eh : mesh.edgeRange()) {
        float cost = decimator.cost(mesh, Graph::CollapseContext(mesh, eh));
        queue.insert(eh, cost);
    }

    /*for (HalfEdgeHandle heh : mesh.halfEdgeRange()) {
        if (!mesh.valid(heh)) {
            continue;
        }
        EdgeHandle eh = mesh.edge(heh);
        HalfEdgeHandle h = mesh.halfEdge(eh);
        float cost = decimator.cost(mesh, Graph::CollapseContext(mesh, heh));
        queue.update(h, cost);
    }*/

    std::size_t cnt = 0;
    EdgeHandle collapsedEdge;
    float c;
    while (!queue.empty()) {
        std::tie(collapsedEdge, c) = queue.pop();
        if (mesh.removed(collapsedEdge)) {
            continue;
        }
        Graph::CollapseContext context(mesh, collapsedEdge);
        if (mesh.collapseAllowed(context.edge)) {
            Vec target = decimator.placement(mesh, context);
            /*std::vector<HalfEdgeHandle> ring;
            for (HalfEdgeHandle eh : mesh.halfEdgeRing(mesh.from(collapsedEdge))) {
                ring.push_back(eh);
            }
            for (HalfEdgeHandle eh : mesh.halfEdgeRing(mesh.to(collapsedEdge))) {
                ring.push_back(eh);
            }
            */
            std::set<VertexHandle> ring;
            for (VertexHandle vh : mesh.vertexRing(context.remaining)) {
                if (vh != context.removed) {
                    ring.insert(vh);
                    queue.remove(mesh.edge(vh, context.remaining));
                }
            }
            for (VertexHandle vh : mesh.vertexRing(context.removed)) {
                if (vh != context.remaining) {
                    ring.insert(vh);
                    queue.remove(mesh.edge(vh, context.removed));
                }
            }
            std::cout << "# " << cnt << " Collapsing " << mesh.to(context.edge) << " into "
                      << mesh.from(context.edge) << ", cost = " << c << std::endl;
            /*std::cout << "ring:\n";
            for (HalfEdgeHandle eh : ring) {
                std::cout << mesh.from(eh) << "-" << mesh.to(eh) << "\n";
            }
            std::cout << "faces:\n";
            for (HalfEdgeHandle eh : ring) {
                std::array<int, 3> is = mesh.faceIndices(mesh.left(eh));
                std::cout << is[0] << " " << is[1] << " " << is[2] << std::endl;
            }
            std::cout << std::endl;
             savePatch(mesh, collapsedEdge);*/
            mesh.collapse(context.edge, target);
            for (VertexHandle vh : ring) {
                EdgeHandle eh = mesh.edge(context.remaining, vh);
                PVL_ASSERT(!mesh.removed(eh));
                PVL_ASSERT(mesh.valid(eh));

                // HalEdgeHandle heh1 = mesh.halfEdge(mesh.edge(heh));
                // PVL_ASSERT(!mesh.removed(heh1));
                float cost = decimator.cost(mesh, Graph::CollapseContext(mesh, eh));
                queue.insert(eh, cost);
            }
            decimator.postprocess(mesh, context);
            if (stop(cnt++)) {
                return;
            }
        }
    }
}

} // namespace Pvl