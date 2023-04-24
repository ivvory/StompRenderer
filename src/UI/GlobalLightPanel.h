#pragma once

#include <memory>
#include "ImguiUnit.h"

namespace omp
{

    struct Light;

    class GlobalLightPanel : public ImguiUnit
    {
    private:
        std::weak_ptr<omp::Light> m_LightRef;

    public:
        explicit GlobalLightPanel(const std::shared_ptr<omp::Light>& inLight);

        virtual void renderUi(float deltaTime);
    };
}
