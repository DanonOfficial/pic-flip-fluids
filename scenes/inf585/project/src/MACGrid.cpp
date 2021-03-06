#include <cmath>
#include <vector>
#include <cstdio>
#include "MACGrid.h"
#include "vcl/vcl.hpp"
#include "boundaries.h"

using namespace std;

MACGrid::MACGrid(int xCellCount, int yCellCount, float cellSize) : xCellCount(xCellCount), yCellCount(yCellCount),
                                                                   cellSize(cellSize) {
    u = vcl::grid_2D<float>(xCellCount + 1, yCellCount);
    du = vcl::grid_2D<float>(xCellCount + 1, yCellCount);
    v = vcl::grid_2D<float>(xCellCount, yCellCount + 1);
    dv = vcl::grid_2D<float>(xCellCount, yCellCount + 1);
    cellTypes = vcl::grid_2D<cellType>(xCellCount, yCellCount);
    distanceField = vcl::grid_2D<float>(xCellCount, yCellCount);
    div = vcl::grid_2D<float>(xCellCount, yCellCount);
}



void MACGrid::saveFlipVelocities() {
    du = u;
    dv = v;
}

void MACGrid::updateExternalForces(float dt) {
    const float g = 9.81;
    v -= dt * g;
}

void MACGrid::updateDistanceField() {

    distanceField.fill(std::numeric_limits<float>::max());
    for (size_t j = 1; j < distanceField.dimension.y - 1; j++) {
        for (size_t i = 1; i < distanceField.dimension.x - 1; i++) {
            if (cellTypes(i, j) == FLUID_CELL) {
                distanceField(i, j) = 0.0;
            }
        }
    }
    auto updateFunc = [this](int i, int j, int di, int dj) {
        if (cellTypes(i, j) != cellType::FLUID_CELL) {
            float a = distanceField(i - di, j);
            float b = distanceField(i, j - dj);
            float initDistance = std::min(a, b) + 1;
            if (initDistance > std::max(a, b)) {
                initDistance = static_cast<float>(a + b + std::sqrt(2.f - std::pow(a - b, 2))) / 2;
            }
            distanceField(i, j) = std::min(distanceField(i, j), initDistance);
        }
    };
    for (size_t i = 0; i < 4; ++i) {
        performSweep(1, distanceField.dimension[0], 1, distanceField.dimension[1], updateFunc);
        performSweep(1, distanceField.dimension[0], static_cast<int>(distanceField.dimension[1] - 2), 0, updateFunc);
        performSweep(static_cast<int>(distanceField.dimension[0] - 2), 0, 1, distanceField.dimension[1], updateFunc);
        performSweep(static_cast<int>(distanceField.dimension[0] - 2), 0, static_cast<int>(distanceField.dimension[1] - 2), 0, updateFunc);
    }
}

void MACGrid::interpolateVelocities() {
    sweepU();
    sweepV();
}

void MACGrid::updateBoundaries() {
    for (size_t i = 0; i < cellTypes.dimension[0]; i++) {
        cellTypes(i, 0u) = cellType::SOLID_CELL;
        cellTypes(i, cellTypes.dimension[1] - 1) = cellType::SOLID_CELL;
    }
    for (size_t i = 0; i < cellTypes.dimension[1]; i++) {
        cellTypes(0u, i) = cellType::SOLID_CELL;
        cellTypes(cellTypes.dimension[0] - 1, i) = cellType::SOLID_CELL;
    }

    for (size_t i = 0; i < u.dimension[1]; i++) {
        u(0u, i) = 0;
        u(1u, i) = 0;
        u(u.dimension[0] - 1, i) = 0;
        u(u.dimension[0] - 2, i) = 0;
    }
    for (size_t i = 0; i < v.dimension[0]; i++) {
        v(i, 0u) = 0;
        v(i, 1u) = 0;
        v(i, v.dimension[1] - 1) = 0;
        v(i, v.dimension[1] - 2) = 0;
    }
}

void MACGrid::divFreeField() {
    calculateDiv();
    vcl::grid_2D<float> q = vcl::grid_2D<float>(div.dimension[0], div.dimension[1]);
    q.fill(0);
    set_boundary(q);
    for (size_t k_iter = 0; k_iter < 50; k_iter++) {
        for (size_t x = 1; x < div.dimension[0] - 1; x++) {
            for (size_t y = 1; y < div.dimension[1] - 1; y++) {
                if (cellTypes(x, y) == FLUID_CELL) {
                    q(x, y) = (q(x + 1, y) + q(x - 1, y) + q(x, y + 1) + q(x, y - 1) - div(x, y)) / 4.f;
                }
            }
        }
        set_boundary(q);
    }

    for (size_t x = 2; x < u.dimension[0] - 2; x++) {
        for (size_t y = 1; y < u.dimension[1] - 1; y++) {
            if ((cellTypes(x, y) == FLUID_CELL && cellTypes(x - 1, y) != SOLID_CELL) ||
                (cellTypes(x - 1, y) == FLUID_CELL && cellTypes(x, y) != SOLID_CELL)) {
                u(x, y) = u(x, y) - (q(x, y) - q(x - 1, y));
            }
        }
    }
    for (size_t x = 1; x < v.dimension[0] - 1; x++) {
        for (size_t y = 2; y < v.dimension[1] - 2; y++) {
            if ((cellTypes(x, y) == FLUID_CELL && cellTypes(x, y - 1) != SOLID_CELL) ||
                (cellTypes(x, y) != SOLID_CELL && cellTypes(x, y - 1) == FLUID_CELL)) {
                v(x, y) = v(x, y) - (q(x, y) - q(x, y - 1));
            }

        }
    }
    set_boundary(u);
    set_boundary(v);

}

void MACGrid::updateVelocities() {
    du = u - du;
    dv = v - dv;
}

void MACGrid::sweepU() {
    auto updateFunc = [this](int i, int j, int di, int dj) {
        if (cellTypes(i, j) == cellType::EMPTY_CELL && cellTypes(i - 1, j) == cellType::EMPTY_CELL) {
            float distanceDeltaI = distanceField(i, j) - distanceField(i - di, j);
            if (distanceDeltaI < 0) {
                return;
            }
            float distanceDeltaJ = distanceField(i, j) - distanceField(i, j - dj);
            if (distanceDeltaJ < 0) {
                return;
            }
            float coeff;
            if (distanceDeltaI + distanceDeltaJ == 0) {
                coeff = 0.5;
            } else {
                coeff = distanceDeltaI / (distanceDeltaI + distanceDeltaJ);
            }
            u(i, j) = coeff * u(i - di, j) + (1 - coeff) * u(i, j - dj);
        }
    };

    for (size_t k = 0; k < 4; k++) {
        performSweep(1, distanceField.dimension[0], 1, distanceField.dimension[1], updateFunc);
        performSweep(1, distanceField.dimension[0], static_cast<int>(distanceField.dimension[1] - 2), 0, updateFunc);
        performSweep(static_cast<int>(distanceField.dimension[0] - 2), 0, 1, distanceField.dimension[1], updateFunc);
        performSweep(static_cast<int>(distanceField.dimension[0] - 2), 0, static_cast<int>(distanceField.dimension[1] - 2), 0, updateFunc);
    }
    for (size_t i = 0; i < u.dimension[0]; i++) {
        u(i, 0u) = u(i, 1u);
        u(i, u.dimension[1] - 1) = u(i, u.dimension[1] - 2);
    }
    for (size_t i = 0; i < u.dimension[1]; i++) {
        u(0u, i) = u(1u, i);
        u(u.dimension[0] - 1, i) = u(u.dimension[0] - 2, i);
    }
}

void MACGrid::sweepV() {
    auto updateFunc = [this](int i, int j, int di, int dj) {
        if (cellTypes(i, j) == cellType::EMPTY_CELL && cellTypes(i, j - 1) == cellType::EMPTY_CELL) {
            float distanceDeltaI = distanceField(i, j) - distanceField(i, j - dj);
            if (distanceDeltaI < 0) {
                return;
            }
            float distanceDeltaJ = distanceField(i, j) - distanceField(i - di, j);
            if (distanceDeltaJ < 0) {
                return;
            }
            float coeff;
            if (distanceDeltaI + distanceDeltaJ == 0) {
                coeff = 0.5;
            } else {
                coeff = distanceDeltaJ / (distanceDeltaI + distanceDeltaJ);
            }
            v(i, j) = coeff * v(i - di, j) + (1 - coeff) * v(i, j - dj);
        }
    };

    for (size_t k = 0; k < 2; k++) {
        performSweep(1, distanceField.dimension[0], 1, distanceField.dimension[1], updateFunc);
        performSweep(1, distanceField.dimension[0], static_cast<int>(distanceField.dimension[1] - 2), 0, updateFunc);
        performSweep(static_cast<int>(distanceField.dimension[0] - 2), 0, 1, distanceField.dimension[1], updateFunc);
        performSweep(static_cast<int>(distanceField.dimension[0] - 2), 0, static_cast<int>(distanceField.dimension[1] - 2), 0, updateFunc);
    }

    for (size_t i = 0; i < v.dimension[0]; i++) {
        v(i, 0u) = v(i, 1u);
        v(i, v.dimension[1] - 1) = v(i, v.dimension[1] - 2);
    }
    for (size_t i = 0; i < v.dimension[1]; i++) {
        v(0u, i) = v(1u, i);
        v(v.dimension[0] - 1, i) = v(v.dimension[0] - 2, i);
    }
}

void MACGrid::calculateDiv() {
    div.fill(0);
    for (int j = 0; j < static_cast<int>(div.dimension.y); j++) {
        for (int i = 0; i < static_cast<int>(div.dimension.x); i++) {
            if (cellTypes(i, j) == FLUID_CELL) {
                div(i, j) = u(i + 1, j) - u(i, j) + v(i, j + 1) - v(i, j);
            }
        }
    }
}

void MACGrid::performSweep(int fromX, int toX, int fromY, int toY,
                           const std::function<void(int, int, int, int)> &function) {
    int di = fromX <= toX ? 1 : -1;
    int dj = fromY <= toY ? 1 : -1;
    for (int j = fromY; j != toY; j += dj) {
        for (int i = fromX; i != toX; i += di) {
            function(i, j, di, dj);
        }
    }
}


barycentricCoords MACGrid::barycentricOnY(float y) const {
    float cellsCoord = y / cellSize;
    int cellIndex = static_cast<int>(cellsCoord);
    if (cellIndex < 0) {
        return {0, 0.f};
    } else if (cellIndex > static_cast<int>(yCellCount - 2)) {
        return {yCellCount - 2, 1.f};
    } else {
        return {static_cast<size_t>(cellsCoord), cellsCoord - std::floor(cellsCoord)};
    }
}

barycentricCoords MACGrid::barycentricOnX(float x) const {
    float cellsCoord = x / cellSize;
    int cellIndex = static_cast<int>(cellsCoord);
    if (cellIndex < 0) {
        return {0, 0.f};
    } else if (cellIndex > static_cast<int>(xCellCount - 2)) {
        return {xCellCount - 2, 1.f};
    } else {
        return {static_cast<size_t>(cellsCoord), cellsCoord - std::floor(cellsCoord)};
    }
}

void MACGrid::update(float dt) {
    saveFlipVelocities();
    updateExternalForces(dt);
    updateDistanceField();
    interpolateVelocities();
    updateBoundaries();
    divFreeField();
    interpolateVelocities();
    updateVelocities();
}

