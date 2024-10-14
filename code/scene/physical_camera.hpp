#pragma once
#include <glm/gtc/matrix_inverse.hpp>
#include <reactive/reactive.hpp>

class PhysicalCamera : public rv::Camera {
public:
    PhysicalCamera() = default;

    PhysicalCamera(rv::Camera::Type type, float aspect) : rv::Camera(type, aspect) {}

    float getImageDistance() const { return m_sensorHeight / (2.0f * std::tan(fovY / 2.0f)); }

    bool drawAttributes() {
        bool changed = false;
        if (ImGui::CollapsingHeader("Camera")) {
            ImGui::Indent(16.0f);

            // Base camera params
            if (ImGui::Combo("Type", reinterpret_cast<int*>(&type), "Orbital\0FirstPerson")) {
                changed = true;
            }
            if (ImGui::DragFloat3("Rotation", &eulerRotation[0], 0.01f)) {
                changed = true;
            }
            if (type == Type::Orbital) {
                auto& _params = std::get<OrbitalParams>(params);
                if (ImGui::DragFloat3("Target", &_params.target[0], 0.1f)) {
                    changed = true;
                }
                if (ImGui::DragFloat("Distance", &_params.distance, 0.1f)) {
                    changed = true;
                }
            } else {
                // TODO: FirstPerson
            }

            // Physical params
            float fovYDeg = glm::degrees(fovY);
            if (ImGui::DragFloat("FOV Y", &fovYDeg, 1.0f, 0.0f, 180.0f)) {
                fovY = glm::radians(fovYDeg);
                changed = true;
            }
            if (ImGui::DragFloat("Lens radius", &m_lensRadius, 0.01f, 0.0f, 1.0f)) {
                changed = true;
            }
            if (ImGui::DragFloat("Object distance", &m_objectDistance, 0.01f, 0.0f)) {
                changed = true;
            }

            ImGui::Unindent(16.0f);
        }
        return changed;
    }

    float m_lensRadius = 0.0f;
    float m_objectDistance = 5.0f;
    static constexpr float m_sensorHeight = 1.0f;
};
