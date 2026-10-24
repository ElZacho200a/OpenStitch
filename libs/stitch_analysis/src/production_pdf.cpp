// SPDX-License-Identifier: Apache-2.0
#include "openstitch/stitch_analysis/production_pdf.hpp"

#include <fmt/format.h>

#include <algorithm>
#include <cstdint>
#include <vector>

namespace openstitch::stitch_analysis {

namespace {

constexpr double kPageW = 595.0; // A4 en points PDF
constexpr double kPageH = 842.0;
constexpr double kMargin = 42.0;
constexpr double kPtPerMm = 72.0 / 25.4;

// UTF-8 -> octets WinAnsi (cp1252) ; hors jeu : '?'.
std::string to_winansi(const std::string& in) {
    std::string out;
    std::size_t i = 0;
    while (i < in.size()) {
        const auto c = static_cast<unsigned char>(in[i]);
        std::uint32_t cp = c;
        std::size_t len = 1;
        if (c >= 0xF0) {
            len = 4;
            cp = c & 0x07U;
        } else if (c >= 0xE0) {
            len = 3;
            cp = c & 0x0FU;
        } else if (c >= 0xC0) {
            len = 2;
            cp = c & 0x1FU;
        }
        if (i + len > in.size()) {
            out += '?';
            break;
        }
        for (std::size_t k = 1; k < len; ++k) {
            cp = (cp << 6U) | (static_cast<unsigned char>(in[i + k]) & 0x3FU);
        }
        i += len;
        if (cp == '\n' || cp == '\r' || cp == '\t') {
            out += ' ';
        } else if (cp >= 0x20 && cp < 0x7F) {
            out += static_cast<char>(cp);
        } else if (cp >= 0xA0 && cp <= 0xFF) {
            out += static_cast<char>(cp);
        } else {
            switch (cp) {
            case 0x20AC:
                out += static_cast<char>(0x80);
                break;
            case 0x2018:
                out += static_cast<char>(0x91);
                break;
            case 0x2019:
                out += static_cast<char>(0x92);
                break;
            case 0x201C:
                out += static_cast<char>(0x93);
                break;
            case 0x201D:
                out += static_cast<char>(0x94);
                break;
            case 0x2013:
                out += static_cast<char>(0x96);
                break;
            case 0x2014:
                out += static_cast<char>(0x97);
                break;
            case 0x0153:
                out += static_cast<char>(0x9C);
                break;
            case 0x0152:
                out += static_cast<char>(0x8C);
                break;
            default:
                out += '?';
            }
        }
    }
    return out;
}

// Chaîne littérale PDF : parenthèses / antislash échappés, octets >= 0x80 en octal.
std::string pdf_string(const std::string& utf8) {
    std::string s = "(";
    for (const char ch : to_winansi(utf8)) {
        const auto c = static_cast<unsigned char>(ch);
        if (ch == '(' || ch == ')' || ch == '\\') {
            s += '\\';
            s += ch;
        } else if (c >= 0x80) {
            s += fmt::format("\\{:03o}", c);
        } else {
            s += ch;
        }
    }
    s += ')';
    return s;
}

std::string fr(double v, int decimals) {
    std::string s = fmt::format("{:.{}f}", v, decimals);
    std::replace(s.begin(), s.end(), '.', ',');
    return s;
}

// Largeur approchée d'un texte Helvetica (moyenne 0,5 em) : suffisant pour couper.
double text_width(const std::string& s, double size) {
    return static_cast<double>(to_winansi(s).size()) * size * 0.5;
}

std::vector<std::string> wrap(const std::string& text, double size, double width) {
    std::vector<std::string> lines;
    std::string line;
    std::string word;
    auto flush_word = [&]() {
        if (word.empty()) {
            return;
        }
        const std::string tentative = line.empty() ? word : line + " " + word;
        if (!line.empty() && text_width(tentative, size) > width) {
            lines.push_back(line);
            line = word;
        } else {
            line = tentative;
        }
        word.clear();
    };
    for (const char ch : text) {
        if (ch == '\n') {
            flush_word();
            lines.push_back(line);
            line.clear();
        } else if (ch == ' ' || ch == '\r' || ch == '\t') {
            flush_word();
        } else {
            word += ch;
        }
    }
    flush_word();
    if (!line.empty() || lines.empty()) {
        lines.push_back(line);
    }
    return lines;
}

class PdfPages {
public:
    PdfPages() { new_page(); }

    [[nodiscard]] double y() const { return y_; }
    void skip(double dy) { y_ += dy; }
    // Garantit `h` points libres sous le curseur, sinon ouvre une page.
    void need(double h) {
        if (y_ + h > kPageH - kMargin) {
            new_page();
        }
    }
    void new_page() {
        pages_.emplace_back();
        y_ = kMargin;
    }

    // Texte dont la ligne de base est à `y_ + size` ; ne bouge pas le curseur.
    void text(double x, double size, bool bold, const std::string& s, double gray = 0.0) {
        cur() +=
            fmt::format("BT /{} {:.1f} Tf {:.2f} g {:.2f} {:.2f} Td {} Tj ET\n", bold ? "F2" : "F1",
                        size, gray, x, kPageH - (y_ + size), pdf_string(s));
    }
    void rect_fill(double x, double ytop, double w, double h, double r, double g, double b) {
        cur() += fmt::format("{:.3f} {:.3f} {:.3f} rg {:.2f} {:.2f} {:.2f} {:.2f} re f\n", r, g, b,
                             x, kPageH - (ytop + h), w, h);
    }
    void rect_stroke(double x, double ytop, double w, double h, double gray) {
        cur() += fmt::format("{:.2f} G 0.5 w {:.2f} {:.2f} {:.2f} {:.2f} re S\n", gray, x,
                             kPageH - (ytop + h), w, h);
    }
    void hline(double x0, double x1, double ytop, double gray) {
        cur() += fmt::format("{:.2f} G 0.5 w {:.2f} {:.2f} m {:.2f} {:.2f} l S\n", gray, x0,
                             kPageH - ytop, x1, kPageH - ytop);
    }
    void polyline(const std::vector<std::pair<double, double>>& pts, double r, double g, double b) {
        if (pts.size() < 2) {
            return;
        }
        std::string s = fmt::format("{:.3f} {:.3f} {:.3f} RG 0.4 w 1 J 1 j\n", r, g, b);
        s += fmt::format("{:.2f} {:.2f} m\n", pts[0].first, kPageH - pts[0].second);
        for (std::size_t i = 1; i < pts.size(); ++i) {
            s += fmt::format("{:.2f} {:.2f} l\n", pts[i].first, kPageH - pts[i].second);
        }
        s += "S\n";
        cur() += s;
    }

    [[nodiscard]] std::string finish() const {
        // Objets : 1 catalogue, 2 pages, 3 et 4 polices, puis (page, contenu) par page.
        std::vector<std::string> objs;
        objs.push_back("<< /Type /Catalog /Pages 2 0 R >>");
        std::string kids;
        for (std::size_t i = 0; i < pages_.size(); ++i) {
            kids += fmt::format("{} 0 R ", 5 + 2 * i);
        }
        objs.push_back(fmt::format("<< /Type /Pages /Kids [{}] /Count {} >>", kids, pages_.size()));
        objs.push_back("<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica "
                       "/Encoding /WinAnsiEncoding >>");
        objs.push_back("<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica-Bold "
                       "/Encoding /WinAnsiEncoding >>");
        for (std::size_t i = 0; i < pages_.size(); ++i) {
            objs.push_back(
                fmt::format("<< /Type /Page /Parent 2 0 R /MediaBox [0 0 {:.0f} {:.0f}] "
                            "/Resources << /Font << /F1 3 0 R /F2 4 0 R >> >> /Contents {} 0 R >>",
                            kPageW, kPageH, 6 + 2 * i));
            // Le contenu suit la page : on insère l'objet flux juste après.
            objs.push_back(
                fmt::format("<< /Length {} >>\nstream\n{}endstream", pages_[i].size(), pages_[i]));
        }
        std::string out = "%PDF-1.4\n%\xE2\xE3\xCF\xD3\n";
        std::vector<std::size_t> offsets;
        for (std::size_t i = 0; i < objs.size(); ++i) {
            offsets.push_back(out.size());
            out += fmt::format("{} 0 obj\n{}\nendobj\n", i + 1, objs[i]);
        }
        const std::size_t xref = out.size();
        out += fmt::format("xref\n0 {}\n0000000000 65535 f \n", objs.size() + 1);
        for (const auto off : offsets) {
            out += fmt::format("{:010} 00000 n \n", off);
        }
        out += fmt::format("trailer\n<< /Size {} /Root 1 0 R >>\nstartxref\n{}\n%%EOF\n",
                           objs.size() + 1, xref);
        return out;
    }

private:
    std::string& cur() { return pages_.back(); }
    std::vector<std::string> pages_;
    double y_{kMargin};
};

} // namespace

std::string production_to_pdf(const ProductionSheet& s) {
    PdfPages pdf;
    constexpr double kContentW = kPageW - 2 * kMargin;

    pdf.text(kMargin, 20, true, "Fiche de production");
    pdf.skip(26);
    pdf.text(kMargin, 13, true, s.project_name.empty() ? "(sans titre)" : s.project_name);
    pdf.skip(18);
    std::string meta = s.date;
    if (!s.client.empty()) {
        meta += (meta.empty() ? "" : "   -   ") + std::string("Client : ") + s.client;
    }
    if (!meta.empty()) {
        pdf.text(kMargin, 10, false, meta, 0.3);
        pdf.skip(14);
    }
    pdf.hline(kMargin, kPageW - kMargin, pdf.y() + 2, 0.6);
    pdf.skip(10);

    // Chiffres clés (deux colonnes).
    std::vector<std::pair<std::string, std::string>> facts;
    facts.emplace_back("Dimensions", fr(s.width_mm, 1) + " x " + fr(s.height_mm, 1) + " mm");
    if (s.frame_mm) {
        facts.emplace_back("Cadre", fr(s.frame_mm->first, 1) + " x " + fr(s.frame_mm->second, 1) +
                                        " mm" + (s.fits_frame ? "" : " (le motif depasse)"));
    }
    facts.emplace_back("Points", std::to_string(s.stitches));
    facts.emplace_back("Sauts / coupes", std::to_string(s.jumps) + " / " + std::to_string(s.trims));
    facts.emplace_back("Changements de fil", std::to_string(s.color_changes));
    facts.emplace_back("Fil estime", fr(s.thread_length_m, 2) + " m");
    facts.emplace_back("Temps estime", format_duration_fr(s.estimated_minutes) + " (" +
                                           fr(s.stitches_per_minute, 0) + " points/min)");
    for (const auto& [k, v] : facts) {
        pdf.need(14);
        pdf.text(kMargin, 10, true, k + " :");
        pdf.text(kMargin + 130, 10, false, v);
        pdf.skip(14);
    }
    pdf.skip(8);

    // Aperçu vectoriel.
    double x0 = 0;
    double y0 = 0;
    double x1 = 10;
    double y1 = 10;
    bool any = false;
    for (const auto& st : s.preview) {
        for (const auto& p : st.points) {
            if (!any) {
                x0 = x1 = p.first;
                y0 = y1 = p.second;
                any = true;
            }
            x0 = std::min<double>(x0, p.first);
            x1 = std::max<double>(x1, p.first);
            y0 = std::min<double>(y0, p.second);
            y1 = std::max<double>(y1, p.second);
        }
    }
    const double dw = std::max(x1 - x0, 1.0);
    const double dh = std::max(y1 - y0, 1.0);
    const double boxW = kContentW;
    const double boxH = 250.0;
    const double scale = std::min(boxW / (dw * kPtPerMm), boxH / (dh * kPtPerMm)) * kPtPerMm;
    pdf.need(boxH + 20);
    pdf.text(kMargin, 11, true, "Apercu du motif");
    pdf.skip(16);
    pdf.rect_fill(kMargin, pdf.y(), boxW, boxH, 0.957, 0.945, 0.918);
    pdf.rect_stroke(kMargin, pdf.y(), boxW, boxH, 0.6);
    const double ox = kMargin + (boxW - dw * scale) / 2.0;
    const double oy = pdf.y() + (boxH - dh * scale) / 2.0;
    for (const auto& st : s.preview) {
        if (s.blocks.empty()) {
            break;
        }
        std::vector<std::pair<double, double>> pts;
        pts.reserve(st.points.size());
        for (const auto& p : st.points) {
            pts.emplace_back(ox + (p.first - x0) * scale, oy + (p.second - y0) * scale);
        }
        const auto& rgb = s.blocks[std::min(st.block, s.blocks.size() - 1)].rgb;
        pdf.polyline(pts, rgb[0] / 255.0, rgb[1] / 255.0, rgb[2] / 255.0);
    }
    pdf.skip(boxH + 14);

    // Blocs de couleur, dans l'ordre de couture.
    pdf.need(40);
    pdf.text(kMargin, 11, true, "Fils dans l'ordre de couture");
    pdf.skip(18);
    auto block_header = [&]() {
        pdf.text(kMargin, 9, true, "N");
        pdf.text(kMargin + 50, 9, true, "Reference de fil");
        pdf.text(kMargin + 300, 9, true, "Points");
        pdf.text(kMargin + 360, 9, true, "Temps");
        pdf.text(kMargin + 420, 9, true, "Longueur");
        pdf.skip(12);
        pdf.hline(kMargin, kPageW - kMargin, pdf.y(), 0.7);
        pdf.skip(3);
    };
    block_header();
    for (const auto& b : s.blocks) {
        if (pdf.y() + 16 > kPageH - kMargin) {
            pdf.new_page();
            block_header();
        }
        pdf.rect_fill(kMargin + 16, pdf.y(), 24, 11, b.rgb[0] / 255.0, b.rgb[1] / 255.0,
                      b.rgb[2] / 255.0);
        pdf.rect_stroke(kMargin + 16, pdf.y(), 24, 11, 0.5);
        pdf.text(kMargin, 9, false, std::to_string(b.number));
        pdf.text(kMargin + 50, 9, false,
                 (b.thread_label.empty()
                      ? fmt::format("#{:02x}{:02x}{:02x}", b.rgb[0], b.rgb[1], b.rgb[2])
                      : b.thread_label));
        pdf.text(kMargin + 300, 9, false, std::to_string(b.stitches));
        pdf.text(kMargin + 360, 9, false, format_duration_fr(b.minutes));
        pdf.text(kMargin + 420, 9, false, fr(b.thread_length_mm / 1000.0, 2) + " m");
        pdf.skip(15);
    }
    pdf.skip(8);

    // Consommation de fil.
    if (!s.consumption.empty()) {
        pdf.need(60);
        pdf.text(kMargin, 11, true, "Consommation de fil estimee");
        pdf.skip(18);
        for (const auto& c : s.consumption) {
            pdf.need(15);
            pdf.rect_fill(kMargin, pdf.y(), 14, 10, c.rgb[0] / 255.0, c.rgb[1] / 255.0,
                          c.rgb[2] / 255.0);
            pdf.text(kMargin + 22, 9, false,
                     c.thread_label.empty()
                         ? fmt::format("#{:02x}{:02x}{:02x}", c.rgb[0], c.rgb[1], c.rgb[2])
                         : c.thread_label);
            pdf.text(kMargin + 300, 9, false, "dessus " + fr(c.top_m, 2) + " m");
            pdf.text(kMargin + 400, 9, false, "canette " + fr(c.bobbin_m, 2) + " m");
            pdf.skip(14);
        }
        pdf.need(30);
        pdf.skip(4);
        pdf.text(kMargin, 9, true,
                 "Total : dessus " + fr(s.total_top_m, 2) + " m, canette " +
                     fr(s.total_bobbin_m, 2) + " m, " + std::to_string(s.bobbins_needed) +
                     " canette(s) de " + fr(s.bobbin_capacity_m, 0) + " m");
        pdf.skip(18);
    }

    // Avertissements.
    if (!s.findings.empty()) {
        pdf.need(40);
        pdf.text(kMargin, 11, true, "Avertissements (" + std::to_string(s.findings.size()) + ")");
        pdf.skip(18);
        for (const auto& f : s.findings) {
            for (const auto& line : wrap("- " + f.message, 9, kContentW)) {
                pdf.need(13);
                pdf.text(kMargin, 9, false, line);
                pdf.skip(12);
            }
        }
        pdf.skip(8);
    }

    // Notes.
    if (!s.notes.empty()) {
        pdf.need(40);
        pdf.text(kMargin, 11, true, "Notes");
        pdf.skip(18);
        for (const auto& line : wrap(s.notes, 10, kContentW)) {
            pdf.need(14);
            pdf.text(kMargin, 10, false, line);
            pdf.skip(13);
        }
    }
    return pdf.finish();
}

} // namespace openstitch::stitch_analysis
