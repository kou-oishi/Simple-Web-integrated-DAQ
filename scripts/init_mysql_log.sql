CREATE DATABASE IF NOT EXISTS daq;
USE daq;

CREATE TABLE IF NOT EXISTS daq_log (
  id BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
  run INT UNSIGNED NOT NULL,
  subrun INT UNSIGNED NOT NULL,
  nevents BIGINT UNSIGNED NOT NULL,
  start_time DATETIME NOT NULL,
  end_time DATETIME NOT NULL,
  status VARCHAR(32) NOT NULL,
  comment TEXT NOT NULL,
  created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
  PRIMARY KEY (id),
  UNIQUE KEY uq_run_subrun (run, subrun),
  KEY idx_start_time (start_time),
  KEY idx_end_time (end_time),
  KEY idx_status (status)
);
