# Core tests: JUCE-free, fast, run everywhere.
add_executable(floorplan_core_tests CoreTests.cpp)
target_link_libraries(floorplan_core_tests PRIVATE floorplan_core)
set_target_properties(floorplan_core_tests PROPERTIES FOLDER Tests)

set(FLOORPLAN_CORE_TESTS
    sine_table_accuracy
    decay_coefficient_reaches_minus_60db
    svf_lowpass_attenuates_highs
    soft_clip_is_bounded_and_transparent_near_zero
    noise_is_zero_mean_and_bounded
    parameter_specs_are_valid
    extreme_parameters_stay_finite
    every_voice_makes_sound_and_goes_idle
    idle_engine_outputs_silence
    velocity_scales_output
    trigger_is_sample_accurate
    mute_and_solo
    open_hat_is_choked_by_closed_hat
    retrigger_is_click_free
    character_off_ignores_its_settings
    velocity_changes_the_tone
    variation_is_bounded_and_reproducible
    choke_group_is_configurable
    color_adds_harmonics_and_is_level_matched
    punch_shapes_the_attack
    character_is_block_size_and_rate_safe
    kick_defaults_match_the_original_kick
    kick_attack_softens_the_onset
    kick_envelope_model_matches_rendered_output
    kick_shape_squares_the_body
    kick_sub_lengthens_the_tail
    kick_click_tone_and_length
    snare_and_clap_defaults_match_the_originals
    snare_envelopes_bound_the_output
    snare_wire_delay_and_tone
    snare_drop_and_ring_change_the_shell
    clap_envelope_controls
    envelope_curves_describe_the_voices
    metal_and_tone_defaults_match_the_originals
    metal_attack_hold_and_tail_follow_the_envelope
    metal_detune_high_cut_and_stick
    tone_envelope_model_matches_rendered_output
    tone_noise_tone_and_length
    tone_metal_tune_and_shape
    buffer_size_edge_cases
    sample_rate_change_preserves_decay_time
    aux_routing
    random_fuzz_never_produces_nan
    sample_voice_plays_and_slots_hand_off
    sequencer_step_timing_is_exact
    sequencer_is_block_size_invariant
    swing_delays_odd_steps
    microtiming_shifts_steps
    probability_is_statistical_and_deterministic
    ratchet_and_flam_add_hits
    transport_loop_does_not_duplicate_or_drop
    start_mid_pattern_and_stop
    tempo_change_follows_host
    pattern_switch_waits_for_cycle_end
    pattern_chain_advances
    euclidean_patterns
    pattern_operations
    triple_buffer_is_consistent_under_concurrency)

foreach(test_name IN LISTS FLOORPLAN_CORE_TESTS)
    add_test(NAME core.${test_name} COMMAND floorplan_core_tests ${test_name})
endforeach()

# Plugin tests: exercise the JUCE processor (state, presets, serialisation).
# The plugin sources are compiled into a console app (rather than linking the
# plugin's shared-code library) so the JUCE modules are built exactly once.
if(TARGET Floorplan)
    juce_add_console_app(floorplan_plugin_tests PRODUCT_NAME "floorplan_plugin_tests")
    target_sources(floorplan_plugin_tests PRIVATE PluginTests.cpp ${FLOORPLAN_PLUGIN_SOURCES})
    target_compile_definitions(floorplan_plugin_tests PRIVATE
        ${FLOORPLAN_JUCE_DEFINITIONS}
        JucePlugin_Name="Floorplan")
    target_link_libraries(floorplan_plugin_tests PRIVATE
        floorplan_core
        floorplan_fonts
        juce::juce_audio_utils
        juce::juce_audio_formats
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)
    set_target_properties(floorplan_plugin_tests PROPERTIES FOLDER Tests)

    set(FLOORPLAN_PLUGIN_TESTS
        processor_produces_audio_from_midi
        internal_transport_plays_default_pattern
        host_transport_drives_sequencer
        state_round_trip
        legacy_state_migration
        pattern_json_round_trip
        pattern_json_rejects_garbage
        factory_presets_load
        bus_layouts
        multi_output_routing_in_every_layout
        parameters_are_automatable
        advanced_parameters_keep_existing_indices
        old_state_sets_new_parameters_to_default
        midi_note_map_and_velocity
        all_sound_off_silences_and_all_notes_off_does_not
        host_play_ends_the_preview
        invalid_host_timing_is_safe
        sequencer_can_be_switched_off
        midi_and_sequencer_do_not_double
        automation_fast_and_slow_reaches_the_engine
        missing_sample_is_found_by_name
        any_block_size_and_sample_rate_keeps_time
        character_and_choke_controls_change_the_sound)

    foreach(test_name IN LISTS FLOORPLAN_PLUGIN_TESTS)
        add_test(NAME plugin.${test_name} COMMAND floorplan_plugin_tests ${test_name})
    endforeach()
endif()
