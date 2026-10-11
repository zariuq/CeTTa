static void prime_replay_comparison_storage(const PrimeReplayComparison *read,
    CettaStorageCensus *census) {
    if(!read->ready)return;
    arena_storage_census(&read->inspection,census);
    if(read->globals)atom_deep_copy_session_storage_census(read->globals,census);
    cetta_storage_array(census,read->addresses,read->capacity,sizeof(*read->addresses),0u);
    cetta_storage_array(census,read->atoms,read->capacity,sizeof(*read->atoms),0u);
    cetta_storage_array(census,read->atom_domains,read->capacity,sizeof(*read->atom_domains),0u);
    cetta_storage_array(census,read->arenas,read->capacity,sizeof(*read->arenas),0u);
    cetta_storage_array(census,read->physical,read->physical_capacity,sizeof(*read->physical),0u);
    cetta_storage_array(census,read->identities.items,read->identities.capacity,sizeof(*read->identities.items),0u);
}
static void prime_replay_checkpoint_origin_storage(const PrimeReplayCheckpoint *checkpoint,
    CettaStorageCensus *census) {
    if(!checkpoint->ambient_read||!cetta_storage_object(census,checkpoint->ambient_read,
        sizeof(*checkpoint->ambient_read),CETTA_STORAGE_BLOCK))return;
    prime_replay_comparison_storage(checkpoint->ambient_read,census);
}
static void prime_replay_frame_storage(const PrimeReplayRequestFrame *f,CettaStorageCensus *census) {
    if(!f->ready)return;
    arena_storage_census(&f->storage,census);
    arena_storage_census(&f->entry.storage,census);arena_storage_census(&f->post.storage,census);
    prime_replay_checkpoint_origin_storage(&f->entry,census);prime_replay_checkpoint_origin_storage(&f->post,census);
    if(f->entry.preparation)prime_checkpoint_storage(f->entry.preparation,census);
    if(f->post.preparation)prime_checkpoint_storage(f->post.preparation,census);
    prime_native_outcomes_storage(&f->pending,census);
    if(f->results.items!=f->results.inline_items)
        cetta_storage_array(census,f->results.items,f->results.cap,sizeof(*f->results.items),0u);
    cetta_storage_array(census,f->physical.events,f->physical.capacity,sizeof(*f->physical.events),0u);
    if(f->resource.ready) {
        prime_replay_resource_word_storage(&f->resource,census);
        prime_replay_comparison_storage(&f->resource.origin,census);
        cetta_storage_array(census,f->resource.boundaries,f->resource.event_capacity,
            sizeof(*f->resource.boundaries),0u);
        cetta_storage_array(census,f->resource.sites.items,f->resource.sites.capacity,
            sizeof(*f->resource.sites.items),0u);
        cetta_storage_array(census,f->resource.sites.destinations,f->resource.sites.destination_capacity,
            sizeof(*f->resource.sites.destinations),0u);
    }
    cetta_storage_array(census,f->phases.items,f->phases.capacity,sizeof(*f->phases.items),0u);
    cetta_storage_array(census,f->phases.destinations,f->phases.destination_capacity,sizeof(*f->phases.destinations),0u);
    if(f->owns_commands) {
        cetta_storage_array(census,f->commands.commands,f->commands.command_capacity,sizeof(*f->commands.commands),0u);
        cetta_storage_array(census,f->commands.destinations,f->commands.destination_capacity,sizeof(*f->commands.destinations),0u);
    }
}
static void prime_replay_attempt_storage(const PrimeReplayAttempt *attempt,CettaStorageCensus *census) {
    if(!attempt||!cetta_storage_object(census,attempt,sizeof(*attempt),CETTA_STORAGE_BLOCK))return;
    prime_replay_frame_storage(&attempt->frame,census);
    prime_replay_comparison_storage(&attempt->source_read,census);
    prime_replay_comparison_storage(&attempt->entry_read,census);
    prime_replay_comparison_storage(&attempt->copy_read,census);
    prime_replay_comparison_storage(&attempt->post_read,census);
    prime_replay_comparison_storage(&attempt->resource_read,census);
}
