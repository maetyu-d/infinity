#pragma once
#include "Universe.hpp"
#include <map>
namespace infinite {
class Navigation {
  public:
    explicit Navigation(const Universe&);
    Address anchor;
    std::optional<Address> selected;
    double logDistance = 21, targetLogDistance = 21;
    glm::dquat orientation{1, 0, 0, 0};
    glm::dvec3 offset{};
    bool transitioning = false;
    glm::dvec3 cameraPosition() const {
        return offset + orientation * glm::dvec3(0, 0, 1);
    }
    void update(double dt);
    void rotate(double dx, double dy);
    void move(glm::dvec3 axes, double dt, bool fast);
    void zoom(double wheel);
    void select(const Address&);
    void focus();
    void focus(Address);
    void parent();
    void reset();
    std::string serialize() const;
    bool restore(std::string_view);

  private:
    struct View {
        Address address;
        std::optional<Address> selected;
        double logDistance;
        glm::dquat orientation;
        glm::dvec3 offset;
    };
    enum class Motion { Focus, Zoom };
    const Universe& universe_;
    bool explicitSelection_ = false;
    glm::dquat targetOrientation_{1, 0, 0, 0};
    glm::dvec3 targetOffset_{};
    Motion motion_ = Motion::Focus;
    double focusStartLog_ = 0, focusProgress_ = 0;
    glm::dvec3 focusStartOffset_{};
    glm::dquat focusStartOrientation_{1, 0, 0, 0};
    std::map<Address, View> views_;
    View view() const;
    void remember();
    void rebase(const Address&);
    void selectDefault();
};
} // namespace infinite
