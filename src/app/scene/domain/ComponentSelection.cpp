#include "ComponentSelection.h"

#include "app/scene/domain/EditMesh.h"
#include "app/viewport/domain/ViewportTools.h"

#include <algorithm>
#include <array>
#include <set>
#include <span>
#include <vector>

namespace toki::scene {

namespace {

using viewport::SelectionMode;

[[nodiscard]] std::set<ComponentRef> listSelectedVertices(const std::set<ComponentRef>& components,
                                                          const SelectionMode mode,
                                                          const EditMesh& mesh) {
    if (mode == SelectionMode::Vertex) {
        return components;
    }
    std::set<ComponentRef> vertices;
    for (const ComponentRef& component : components) {
        if (component.piece >= mesh.pieces.size()) {
            continue;
        }
        const EditPiece& piece = mesh.pieces[component.piece];
        if (mode == SelectionMode::Edge && component.index < piece.edges.size()) {
            for (const usize vertex : piece.edges[component.index].vertices) {
                vertices.insert({.piece = component.piece, .index = vertex});
            }
        } else if (mode == SelectionMode::Face && component.index < piece.faces.size()) {
            for (const usize vertex : piece.faces[component.index]) {
                vertices.insert({.piece = component.piece, .index = vertex});
            }
        }
    }
    return vertices;
}

[[nodiscard]] std::set<ComponentRef> listHeldEdges(const std::set<ComponentRef>& vertices,
                                                   const EditMesh& mesh) {
    std::set<ComponentRef> edges;
    for (usize piece = 0; piece < mesh.pieces.size(); ++piece) {
        const std::vector<MeshEdge>& pieceEdges = mesh.pieces[piece].edges;
        for (usize edge = 0; edge < pieceEdges.size(); ++edge) {
            const std::array<usize, 2>& ends = pieceEdges[edge].vertices;
            if (vertices.contains({.piece = piece, .index = ends[0]}) &&
                vertices.contains({.piece = piece, .index = ends[1]})) {
                edges.insert({.piece = piece, .index = edge});
            }
        }
    }
    return edges;
}

[[nodiscard]] std::set<ComponentRef> listHeldFaces(const std::set<ComponentRef>& vertices,
                                                   const EditMesh& mesh) {
    std::set<ComponentRef> faces;
    for (usize piece = 0; piece < mesh.pieces.size(); ++piece) {
        const std::vector<std::array<usize, 3>>& pieceFaces = mesh.pieces[piece].faces;
        for (usize face = 0; face < pieceFaces.size(); ++face) {
            if (std::ranges::all_of(pieceFaces[face], [&vertices, piece](const usize vertex) {
                    return vertices.contains({.piece = piece, .index = vertex});
                })) {
                faces.insert({.piece = piece, .index = face});
            }
        }
    }
    return faces;
}

} // namespace

void ComponentSelection::setMode(const SelectionMode next, const EditMesh& mesh) {
    if (next == mode || next == SelectionMode::Node) {
        return;
    }
    const std::set<ComponentRef> vertices = listSelectedVertices(components, mode, mesh);
    if (next == SelectionMode::Vertex) {
        components = vertices;
    } else if (next == SelectionMode::Edge) {
        components = listHeldEdges(vertices, mesh);
    } else {
        components = listHeldFaces(vertices, mesh);
    }
    mode = next;
}

void ComponentSelection::pick(const std::span<const ComponentRef> picked,
                              const viewport::PickMode pickMode) {
    if (pickMode == viewport::PickMode::Replace) {
        components.clear();
    }
    for (const ComponentRef& component : picked) {
        if (pickMode == viewport::PickMode::Remove) {
            components.erase(component);
        } else {
            components.insert(component);
        }
    }
}

std::vector<ComponentRef> ComponentSelection::listVertices(const EditMesh& mesh) const {
    const std::set<ComponentRef> vertices = listSelectedVertices(components, mode, mesh);
    return {vertices.begin(), vertices.end()};
}

std::vector<ComponentRef> ComponentSelection::listFaces(const EditMesh& mesh) const {
    if (mode == SelectionMode::Face) {
        return {components.begin(), components.end()};
    }
    const std::set<ComponentRef> faces =
        listHeldFaces(listSelectedVertices(components, mode, mesh), mesh);
    return {faces.begin(), faces.end()};
}

std::vector<ComponentRef> ComponentSelection::listTouchedFaces(const EditMesh& mesh) const {
    if (mode == SelectionMode::Face) {
        return {components.begin(), components.end()};
    }
    std::vector<ComponentRef> faces;
    for (usize piece = 0; piece < mesh.pieces.size(); ++piece) {
        const EditPiece& shown = mesh.pieces[piece];
        for (usize face = 0; face < shown.faces.size(); ++face) {
            const std::array<usize, 3>& corners = shown.faces[face];
            bool isTouched = false;
            for (usize corner = 0; corner < 3 && !isTouched; ++corner) {
                if (mode == SelectionMode::Vertex) {
                    isTouched = components.contains({.piece = piece, .index = corners[corner]});
                    continue;
                }
                const usize from = corners[corner];
                const usize to = corners[(corner + 1) % 3];
                const MeshEdge edge{.vertices = {std::min(from, to), std::max(from, to)}};
                const auto found = std::ranges::lower_bound(shown.edges, edge);
                isTouched = found != shown.edges.end() && *found == edge &&
                            components.contains(
                                {.piece = piece,
                                 .index = static_cast<usize>(found - shown.edges.begin())});
            }
            if (isTouched) {
                faces.push_back({.piece = piece, .index = face});
            }
        }
    }
    return faces;
}

void ComponentSelection::selectFaces(const std::span<const ComponentRef> faces,
                                     const EditMesh& mesh) {
    const std::set<ComponentRef> picked(faces.begin(), faces.end());
    if (mode == SelectionMode::Face) {
        components = picked;
        return;
    }
    const std::set<ComponentRef> vertices = listSelectedVertices(picked, SelectionMode::Face, mesh);
    components = mode == SelectionMode::Vertex ? vertices : listHeldEdges(vertices, mesh);
}

void ComponentSelection::selectVertices(const std::span<const ComponentRef> vertices,
                                        const EditMesh& mesh) {
    const std::set<ComponentRef> picked(vertices.begin(), vertices.end());
    if (mode == SelectionMode::Vertex) {
        components = picked;
    } else if (mode == SelectionMode::Edge) {
        components = listHeldEdges(picked, mesh);
    } else {
        components = listHeldFaces(picked, mesh);
    }
}

void ComponentSelection::keepWithin(const EditMesh& mesh) {
    std::erase_if(components, [this, &mesh](const ComponentRef& component) {
        if (component.piece >= mesh.pieces.size()) {
            return true;
        }
        const EditPiece& piece = mesh.pieces[component.piece];
        switch (mode) {
        case SelectionMode::Vertex:
            return component.index >= piece.vertices.size();
        case SelectionMode::Edge:
            return component.index >= piece.edges.size();
        case SelectionMode::Face:
        case SelectionMode::Node:
            return component.index >= piece.faces.size();
        }
        return true;
    });
}

} // namespace toki::scene
