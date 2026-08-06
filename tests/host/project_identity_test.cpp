#include <apogee/project.hpp>

#include <string_view>

static_assert(apogee::project_name == std::string_view{"Apogee-FC"});
static_assert(apogee::project_slug == std::string_view{"apogee-fc"});

int main() {
    return (apogee::project_name == "Apogee-FC" &&
            apogee::project_slug == "apogee-fc")
               ? 0
               : 1;
}
