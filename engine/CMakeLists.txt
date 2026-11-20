# Engine libraries, listed from the lowest layer to the highest.
# Each one only depends on the layers above it in this list.
add_subdirectory(core)      # kernel: modules, services, events, scheduler, jobs
add_subdirectory(ecs)       # entity-component-system
add_subdirectory(platform)  # window / input interfaces
add_subdirectory(rhi)       # render hardware interface (backend-agnostic)
add_subdirectory(assets)    # CPU-side asset data, loaders, asset manager
add_subdirectory(scene)     # scenes, transforms, hierarchy
add_subdirectory(audio)     # software mixer, sound sources and listeners
add_subdirectory(animation) # keyframe animation of model nodes
add_subdirectory(particles) # particle emitters and their simulation
add_subdirectory(render)    # renderer interfaces and render components
add_subdirectory(ui)        # immediate-mode interface on the overlay
add_subdirectory(physics)   # rigid bodies and collisions
add_subdirectory(app)       # application framework gluing everything together
