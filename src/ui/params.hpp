#pragma once

#include "ui/ui_context.hpp"

#include <functional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace groovebox {

// Param — самодостатній іменований об'єкт параметра. Не знає, у якому вікні він показаний:
// будь-яке вікно (GridView) просто посилається на його ID (наприклад "TRK.CUT", "DLY.FBK").
struct Param {
    std::string id;     // Унікальний ідентифікатор (PREFIX.NAME)
    std::string label;  // Коротка назва для клітинки (<= 9 символів)
    std::function<std::string(const UiContext&)> dyn_label; // опційно: назва залежить від движка
    std::function<std::string(const UiContext&)> value;     // текст значення
    std::function<float(const UiContext&)> norm;            // опційно: 0..1 для смужки
    std::function<void(UiContext&, int dir, bool coarse)> step; // порожньо => read-only

    std::string labelFor(const UiContext& c) const { return dyn_label ? dyn_label(c) : label; }
    bool editable() const { return static_cast<bool>(step); }
};

// Реєстр усіх параметрів (id -> Param). Додати параметр = один виклик add() у params.cpp
class ParamRegistry {
public:
    static ParamRegistry& instance();

    const Param* find(std::string_view id) const;
    void add(Param p);
    std::vector<std::string> ids() const;

private:
    ParamRegistry();
    std::unordered_map<std::string, Param> map_;
};

} // namespace groovebox
