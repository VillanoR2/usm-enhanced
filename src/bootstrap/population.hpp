#pragma once
// Ambient pedestrians and traffic: the game spawns them from 10 m (ramping to
// 40 m) up to 90 m around the player and removes one entity class beyond 55 m,
// so they visibly pop in close to Spider-Man. This moves the spawn ring out.
// usm_mod.ini [Graficos] RadioAparicionMinimo=45  RadioAparicionMaximo=120

namespace population {

inline void install(float minimum, float maximum) {
    if (!(maximum >= 90.0f)) maximum = 90.0f;
    if (maximum > 150.0f) maximum = 150.0f;  // tested limit ("Alto")
    if (!(minimum >= 10.0f)) minimum = 10.0f;
    if (minimum > 60.0f) minimum = 60.0f;
    if (minimum > maximum - 20.0f) minimum = maximum - 20.0f;
    if (minimum == 10.0f && maximum == 90.0f) return;  // stock

    static const BYTE reset_min[] = {0xC7, 0x05, 0xA4, 0x7F, 0x93, 0x00, 0x00, 0x00, 0x20, 0x41};
    static const BYTE reset_max[] = {0xC7, 0x05, 0xA8, 0x7F, 0x93, 0x00, 0x00, 0x00, 0xB4, 0x42};
    static const BYTE cull_op[] = {0xD8, 0x15, 0xF4, 0x5A, 0x8A, 0x00};
    if (!draw_distance::same(0x6BC120, reset_min, sizeof(reset_min)) ||
        !draw_distance::same(0x6D1CEC, reset_min, sizeof(reset_min)) ||
        !draw_distance::same(0x6D1CF6, reset_max, sizeof(reset_max)) ||
        !draw_distance::same(0x6C2F3E, cull_op, sizeof(cull_op)) ||
        draw_distance::at(0x8A5AF4) != 3025.0f) {
        depth_fix::log_line("population: unexpected code, spawn radius left unchanged");
        return;
    }
    draw_distance::write(0x6BC126, &minimum, 4);   // reset of the minimum radius
    draw_distance::write(0x6D1CF2, &minimum, 4);   // initial minimum radius
    draw_distance::write(0x6D1CFC, &maximum, 4);   // initial maximum radius (despawn at 1.1x)
    const float cull = maximum * maximum;          // was 55 m
    if (cull > 3025.0f) draw_distance::write(0x8A5AF4, &cull, 4);
    depth_fix::log_line("population: spawn ring moved out");
}

// Pool sizes: the game creates 10 pedestrians (0x6D1A98 cmp ebp,0Ah) and
// 30 traffic cars (0x6D1D49 mov esi,1Dh; cap density*30 at 0x6D81DA).
// usm_mod.ini [Graficos] CantidadPeatones=24  CantidadTrafico=45
inline float traffic_cap = 30.0f;

inline void install_counts(float pedestrians, float cars) {
    static const BYTE ped_loop[] = {0x83, 0xFD, 0x0A};
    static const BYTE car_loop[] = {0xBE, 0x1D, 0x00, 0x00, 0x00};
    static const BYTE car_cap[] = {0xD8, 0x0D, 0xC0, 0xEE, 0x87, 0x00};
    if (!draw_distance::same(0x6D1A98, ped_loop, sizeof(ped_loop)) ||
        !draw_distance::same(0x6D1D49, car_loop, sizeof(car_loop)) ||
        !draw_distance::same(0x6D81DA, car_cap, sizeof(car_cap)) || draw_distance::at(0x87EEC0) != 30.0f) {
        depth_fix::log_line("population: unexpected code, pool sizes left unchanged");
        return;
    }
    BYTE ped = 10;
    while (ped < 24 && static_cast<float>(ped + 1) <= pedestrians) ++ped;  // tested limit ("Alto")
    DWORD car = 30;
    while (car < 45 && static_cast<float>(car + 1) <= cars) ++car;    // tested limit ("Alto")
    if (ped != 10) draw_distance::write(0x6D1A9A, &ped, 1);
    if (car != 30) {
        const DWORD last = car - 1;
        draw_distance::write(0x6D1D4A, &last, 4);
        traffic_cap = static_cast<float>(car);
        const DWORD cap = reinterpret_cast<ULONG_PTR>(&traffic_cap);
        draw_distance::write(0x6D81DC, &cap, 4);
    }
    char line[64];
    wsprintfA(line, "population: %u pedestrians, %lu traffic cars", static_cast<unsigned>(ped), static_cast<unsigned long>(car));
    depth_fix::log_line(line);
}

}  // namespace population
