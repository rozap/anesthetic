use crate::{constants::WINDOW_SIZE, window::Window};
use serialport::SerialPort;
use std::{fs::File, io, path::Path};
use termion::input::TermRead;

pub enum LogLevel {
    Info,
    Warn,
    Error,
}

pub struct Connection {
    pub oil_temps: Window,
    pub oil_pressures: Window,
    pub coolant_pressures: Window,
    pub voltages: Window,
    pub rpms: Window,
    pub rssis: Window,
    port: Box<dyn SerialPort>,

    pub counter: u128,
    pub error_counter: u128,
    pub events: Vec<(u128, String, LogLevel)>,
}

impl Connection {
    pub fn init(port: Box<dyn SerialPort>) -> Result<Connection, io::Error> {
        Ok(Connection {
            port: port,
            oil_temps: Window::new(WINDOW_SIZE),
            oil_pressures: Window::new(WINDOW_SIZE),
            coolant_pressures: Window::new(WINDOW_SIZE),
            rpms: Window::new(WINDOW_SIZE),
            rssis: Window::new(WINDOW_SIZE),
            voltages: Window::new(WINDOW_SIZE),
            counter: 0,
            error_counter: 0,
            events: vec![(
                0,
                "w e l c o m e  t o  l e m o n s  d o m i n a t i o n".to_owned(),
                LogLevel::Info,
            )],
        })
    }

    pub fn log(&mut self, lvl: LogLevel, msg: String) {
        self.events.push((self.counter, msg, lvl));
    }

    fn inc_error(&mut self) {
        self.error_counter = self.error_counter + 1;
    }

    pub fn read(&mut self) {
        let result = match self.port.read_line() {
            Ok(Some(line)) => {
                self.counter = self.counter + 1;

                match line
                    .split_ascii_whitespace()
                    .collect::<Vec<&str>>()
                    .as_slice()
                {
                    [keyvalue] => {
                        match keyvalue.split(":").collect::<Vec<&str>>().as_slice() {
                            ["P_C", value] => {
                                self.coolant_pressures.put(value);
                                Some((LogLevel::Info, line))
                            }
                            ["P_O", value] => {
                                self.oil_pressures.put(value);
                                Some((LogLevel::Info, line))
                            }
                            ["T_O", value] => {
                                self.oil_temps.put(value);
                                Some((LogLevel::Info, line))
                            }
                            ["VBA", value] => {
                                self.voltages.put(value);
                                Some((LogLevel::Info, line))
                            }
                            ["RPM", value] => {
                                self.rpms.put(value);
                                Some((LogLevel::Info, line))
                            }
                            ["RSI", value] => {
                                self.rssis.put(value);
                                Some((LogLevel::Info, line))
                            }
                            ["FLT", value] => Some((LogLevel::Warn, format!("Fault: {}", value))),
                            _ => {
                                self.inc_error();
                                Some((
                                    LogLevel::Error,
                                    format!("Invalid key value pair {}", keyvalue),
                                ))
                            }
                        }
                    }

                    _ => {
                        if line == "init ok" {
                            None
                        } else if line.is_empty() {
                            None
                        } else {
                            self.inc_error();
                            Some((LogLevel::Error, format!("Invalid data line '{}'", line)))
                        }
                    }
                }
            }
            Ok(None) => Some((LogLevel::Error, format!("Empty line?"))),
            Err(e) => {
                if e.kind() == io::ErrorKind::TimedOut {
                    None
                } else {
                    self.inc_error();
                    Some((LogLevel::Error, format!("Failed to read line: {}", e)))
                }
            }
        };

        match result {
            Some((level, line)) => {
                self.log(level, line.clone());
            }
            None => {
                // noop
            }
        };
    }
}
