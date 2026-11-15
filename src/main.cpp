#include <clocale>
#include <cstdio>
#include <exception>

#include "app.h"
#include "db.h"

int main() {
    std::setlocale(LC_ALL, "");

    try {
        db::open(db::defaultPath());
    } catch (const std::exception& e) {
        std::fprintf(stderr, "sift: %s\n", e.what());
        return 1;
    }

    App app;
    app.run();

    db::close();
    return 0;
}
