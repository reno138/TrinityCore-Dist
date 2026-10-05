-- c9core cluster port: track which node owns each online character.
-- Set to ClusterServer.NodeId on login, cleared to 0 on logout; used for node-scoped
-- crash recovery and by the dead-node handler.
ALTER TABLE `characters`
    ADD COLUMN `owning_node_id` TINYINT UNSIGNED NOT NULL DEFAULT 0 AFTER `online`,
    ADD INDEX `idx_owning_node_id` (`owning_node_id`);
