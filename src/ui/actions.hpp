#pragma once

#include "ui/ui_context.hpp"

#include <functional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace groovebox {

struct Action {
    std::string id;                                     // Унікальний ID, наприклад "SEQ.TRIG", "PARAM.INC"
    std::string label;                                  // Короткий підпис для підказки (до 6 символів, наприклад "TRIG", "DEL")
    std::function<void(UiContext&)> execute;            // Тіло дії
};

class ActionRegistry {
public:
    static ActionRegistry& instance();

    const Action* find(std::string_view id) const;
    void add(Action a);
    bool run(std::string_view id, UiContext& ctx) const;
    std::vector<std::string> ids() const;

private:
    ActionRegistry();
    std::unordered_map<std::string, Action> map_;
};

} // namespace groovebox
