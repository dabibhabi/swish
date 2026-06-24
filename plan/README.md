# plan/

Architecture plans for upcoming swish work.

- **[car_system_port.md](car_system_port.md)** — plan for porting DownPour's car/driving system into swish: reference analysis of `examples/DownPour`, gap analysis, target architecture (CameraController + cockpit mode, flat articulated parts, `car.toml` config pipeline, forward transparent pass as the rain-on-windshield seam), a 5-phase roadmap with verification gates, and risks. Contains mermaid diagrams for ownership, per-frame sequence, driving FSM, render passes, cockpit transform chain, phases, and config flow.

The same concepts are drawn in [`../architecture.excalidraw`](../architecture.excalidraw) — a dark-canvas atlas with one island per concept (app loop, Vulkan core, render passes, scene/geometry, car system, camera system, config pipeline, rain seam, asset facts). The existing [`../docs/car_system.md`](../docs/car_system.md) describes the current minimal car; its state-machine section is superseded by the FSM in the port plan.
