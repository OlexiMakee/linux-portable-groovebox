#pragma once

#include "ui/canvas.hpp"
#include "ui/params.hpp"
#include "ui/ui_context.hpp"

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace groovebox {

// IView — інтерфейс вікна. Вікно відповідає лише за рендер своєї робочої області,
// навігацію курсора та повідомлення про об'єкт у фокусі. Кнопки до вікна не прив'язані —
// це робить Keymap (OCP: нове вікно не вимагає змін у роутері чи HUD).
class IView {
public:
    explicit IView(ViewId id) : id_(id) {}
    virtual ~IView() = default;

    ViewId id() const { return id_; }
    virtual std::string tabTitle(const UiContext&) const { return viewName(id_); }
    virtual ColorRGB accent() const { return palette::kAccentOrange; }

    virtual void render(Canvas& cv, const UiContext& c, Rect area) const = 0;
    virtual void navigate(UiContext& c, int dx, int dy, bool coarse) = 0;

    // Параметр у фокусі (для дій PARAM.INC / PARAM.DEC); nullptr — немає
    virtual const Param* focusedParam(const UiContext&) const { return nullptr; }
    // Ідентифікатор об'єкта у фокусі (для HUD та голосових/текстових посилань користувача)
    virtual std::string focusId(const UiContext&) const { return viewName(id_); }

    virtual void onEnter(UiContext&) {}
    virtual void tick(UiContext&) {}

private:
    ViewId id_;
};

// ---------------- GridView: універсальна таблиця/список параметрів ----------------
enum class GridStyle : uint8_t {
    kTable, // рядки з 1..5 клітинок (SYNTH, FX, MIX)
    kList,  // вертикальний список «назва — значення» (SYS)
    kPopup  // список у модальному вікні поверх іншого вікна (INSPECT)
};

struct GridRow {
    std::string title;              // коротка назва рядка (<= 6 символів), "" — без назви
    std::vector<std::string> cells; // ID параметрів з ParamRegistry
};

struct GridSpec {
    ViewId id = ViewId::kSynth;
    GridStyle style = GridStyle::kTable;
    ColorRGB accent = palette::kAccentOrange;
    std::function<std::string(const UiContext&)> header;    // заголовок (контекст, без підказок кнопок)
    std::function<std::string(const UiContext&)> tab_title; // опційна назва вкладки
    std::function<void(UiContext&, int row)> on_focus;      // опційно: реакція на зміну рядка (MIX -> трек)
    std::vector<GridRow> rows;
    std::function<std::vector<GridRow>(const UiContext&)> dynamic_rows; // опційно: динамічні рядки за типом рушія
};

class GridView : public IView {
public:
    explicit GridView(GridSpec spec);

    std::string tabTitle(const UiContext& c) const override;
    ColorRGB accent() const override { return spec_.accent; }
    void render(Canvas& cv, const UiContext& c, Rect area) const override;
    void navigate(UiContext& c, int dx, int dy, bool coarse) override;
    const Param* focusedParam(const UiContext& c) const override;
    std::string focusId(const UiContext& c) const override;

private:
    std::vector<std::pair<std::string, std::vector<const Param*>>> resolveRows(const UiContext& c) const;
    const Param* cellParam(const UiContext& c, int row, int col) const;
    int rowOf(const UiContext& c) const;
    int colOf(const UiContext& c) const;
    void renderTable(Canvas& cv, const UiContext& c, Rect area) const;
    void renderList(Canvas& cv, const UiContext& c, Rect area, bool popup) const;

    GridSpec spec_;
    std::vector<std::vector<const Param*>> resolved_; // кеш вказівників на Param
};

// ---------------- SeqView: матричний секвенсор ----------------
class SeqView : public IView {
public:
    SeqView() : IView(ViewId::kSeq) {}
    void render(Canvas& cv, const UiContext& c, Rect area) const override;
    void navigate(UiContext& c, int dx, int dy, bool coarse) override;
    std::string focusId(const UiContext& c) const override;
    void tick(UiContext& c) override;
};

// ---------------- SampleView: станція аудіо-запису та лупера ----------------
class SampleView : public IView {
public:
    SampleView() : IView(ViewId::kSample) {}
    std::string tabTitle(const UiContext&) const override { return "AUDIO-REC"; }
    ColorRGB accent() const override { return palette::kAccentCyan; }
    void render(Canvas& cv, const UiContext& c, Rect area) const override;
    void navigate(UiContext& c, int dx, int dy, bool coarse) override;
    const Param* focusedParam(const UiContext& c) const override;
    std::string focusId(const UiContext& c) const override;

private:
    mutable int cursor_row_ = 0;
    mutable int cursor_col_ = 0;
};

using ScopeView = SampleView;

// ---------------- PerfView: живий перформанс та гра на педах ----------------
class PerfView : public IView {
public:
    PerfView() : IView(ViewId::kPerf) {}
    ColorRGB accent() const override { return palette::kParamTrig; }
    void render(Canvas& cv, const UiContext& c, Rect area) const override;
    void navigate(UiContext& c, int dx, int dy, bool coarse) override;
    std::string focusId(const UiContext& c) const override;
};

} // namespace groovebox
