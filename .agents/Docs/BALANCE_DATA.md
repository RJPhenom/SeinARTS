# Authoring with Balance Data

**Document version:** 0.1

Balance Data is an editor authoring workflow for tuning entity Blueprint components or ability
defaults in a flat DataTable. The source Blueprints remain authoritative at runtime; the generated
table is not simulation state.

## 1. Create and scope a profile

1. In the Content Browser, create **SeinARTS > Balance Data Asset**.
2. Choose **Entities** or **Abilities** as the Target Kind.
3. Add the narrowest useful root classes and any excluded subtrees. Use **Preview Matched Targets**
   to verify the exact concrete classes before generating a table.
4. For an entity profile, either leave **Tracked Components** empty to include every eligible
   component found on the matched entities, or use **Add Component Type** to select an explicit
   set. The picker accepts native SeinARTS components and eligible designer-authored Component
   structs found on the currently matched entities.
5. Optionally choose an Output Directory under a mounted content root. An empty path places the
   generated assets beside the profile.

Keep each profile focused on one tuning concern. Smaller tables are easier to review and reduce the
chance of pushing unrelated edits together.

## 2. Gather and edit

1. Select **Gather -> Table** to create or rebuild the generated DataTable from the source classes.
2. Open the generated table and edit the tuning cells. Fixed-point values appear as ordinary decimal
   numbers in the editor; Push converts them back to deterministic fixed-point values.
3. Select **Check Sync** on the profile before Push. A clean result means every table cell still
   matches its bound source property. A difference can be an intentional table edit; structural
   drift requires another Gather.

Gather rebuilds the table from source and discards pending table edits after confirmation. Do not
Gather when the table contains changes that still need to be pushed.

## 3. Push and seal

1. Select **Push Table -> Source**. Only changed values are written to the bound Blueprint defaults.
   Missing classes, stale schemas, and ambiguous output assets fail closed.
2. Run **Check Sync** again. It should report no differences.
3. Save the modified source Blueprints, the profile, and the generated table.
4. Regenerate and save the project's Simulation Content Manifest because Blueprint simulation
   defaults changed, then repeat the project's normal build and multiplayer qualification gates.

Source-control review should include both the intended Blueprint changes and the generated authoring
assets. Do not resolve a stale table by bypassing its identity checks or by treating it as runtime
authority.
