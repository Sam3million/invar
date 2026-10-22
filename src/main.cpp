#include "App.h"
#include "argparse.hpp"
#include "Config.h"
#include "shared_types.h"

int main(int argc, char* argv[])
{
    argparse::ArgumentParser args("Invar");
    args.add_argument("--scene").default_value(std::string("models/Sponza/glTF/Sponza.gltf"));
    args.add_argument("--res").default_value(std::string("2560x1440"));
    args.add_argument("--bench").default_value(false).implicit_value(true);
    args.add_argument("--frames").scan<'i', int>().default_value(1000);
    args.add_argument("--capture-frame").scan<'i', int>().default_value(-1);
    args.add_argument("--warmup").scan<'i', int>().default_value(120);
    args.add_argument("--repeats").scan<'i', int>().default_value(5);
    args.add_argument("--label").default_value(std::string{""});
    args.add_argument("--mode").choices("classic", "meshlet", "meshlet-culling").default_value(std::string("meshlet-culling"));
    args.add_argument("--inFlight").scan<'i', int>().choices("1", "2", "3").default_value(3);
    args.add_argument("--cameraStartPos").nargs(3).scan<'g', float>().default_value(std::vector{9.85929f, 1.04801f, 0.109808f});
    args.add_argument("--cameraStartRot").nargs(4).scan<'g', float>().default_value(std::vector{-0.0219285f, -0.693247f, -0.0211121f, 0.720057f});
    try
    {
        args.parse_args(argc, argv);
    }
    catch (const std::exception& e)
    {
        std::cerr << e.what() << "\n" << args;
        return 1;
    }

    Config cfg;
    cfg.scene = args.get<std::string>("--scene");
    cfg.mode = args.get<std::string>("--mode");
    if (args.is_used("--label")) cfg.label = args.get<std::string>("--label");
    else cfg.label = cfg.mode;
    std::sscanf(args.get<std::string>("--res").c_str(), "%ux%u", &cfg.width, &cfg.height);
    cfg.bench = args.get<bool>("--bench");
    cfg.frames = args.get<int>("--frames");
    cfg.repeats = args.get<int>("--repeats");
    cfg.framesInFlight = args.get<int>("--inFlight");
    cfg.warmup = args.get<int>("--warmup");
    cfg.captureFrame = args.get<int>("--capture-frame");
    auto cameraPos = args.get<std::vector<float>>("--cameraStartPos");
    cfg.cameraStartPos = float3 { cameraPos[0], cameraPos[1], cameraPos[2] };
    auto cameraRot = args.get<std::vector<float>>("--cameraStartRot");
    cfg.cameraStartRot = quatf {cameraRot[0], cameraRot[1], cameraRot[2], cameraRot[3]};

    App app(cfg);
    app.run();

    return 0;
}
