mod connection;
mod constants;
mod util;
mod widgets;
mod window;

use constants::DEFAULT_BG;

use connection::Connection;
use std::path::Path;
use std::time::Duration;
use std::{env, usize};
use std::{error::Error, io};
use termion::{event::Key, input::MouseTerminal, raw::IntoRawMode, screen::AlternateScreen};
use tui::backend::Backend;
use tui::layout::{Corner, Rect};
use tui::widgets::{List, ListItem};
use tui::Frame;
use tui::{
    backend::TermionBackend,
    layout::{Constraint, Direction, Layout},
    style::{Color, Modifier, Style},
    text::{Span, Spans},
    widgets::{Block, Borders, Tabs},
    Terminal,
};
use util::event::{Event, Events};
use util::TabsState;
use widgets::{chart, gauge};

use crate::connection::LogLevel;

struct BsodApp<'a> {
    tabs: TabsState<'a>,
}

struct SensorConstants {
    oil_temp_max: i16,
    oil_pressure_max: i16,
    coolant_pressure_max: i16,
    voltage_max: i16,
    rpm_max: i16,
    rssi_max: i16,
}

const SENSOR_CONSTANTS: SensorConstants = SensorConstants {
    oil_pressure_max: 80,
    oil_temp_max: 260,
    coolant_pressure_max: 80,
    voltage_max: 16 * 1000,
    rpm_max: 7000,
    rssi_max: 0,
};

fn to_volt(v: i16) -> f64 {
    (v as f64) / 1000.0
}
fn render_gauges<'a, B>(frame: &'a mut Frame<B>, area: Rect, connection: &Connection)
where
    B: Backend,
{
    let gauge_chunks = Layout::default()
        .direction(Direction::Vertical)
        .margin(0)
        .constraints(
            [
                Constraint::Percentage(16),
                Constraint::Percentage(16),
                Constraint::Percentage(16),
                Constraint::Percentage(16),
                Constraint::Percentage(16),
                Constraint::Percentage(16),
            ]
            .as_ref(),
        )
        .split(area);

    connection.oil_temps.last().map(|ot| {
        let oil_temp_gauge = gauge(
            format!("Oil Temperature"),
            format!("{} F", ot),
            *ot,
            SENSOR_CONSTANTS.oil_temp_max,
        );
        frame.render_widget(oil_temp_gauge, gauge_chunks[0]);
    });

    connection.oil_pressures.last().map(|op| {
        let oil_pressure_gauge = gauge(
            format!("Oil Pressure"),
            format!("{} PSI", op),
            *op,
            SENSOR_CONSTANTS.oil_pressure_max,
        );
        frame.render_widget(oil_pressure_gauge, gauge_chunks[1]);
    });

    connection.coolant_pressures.last().map(|cp| {
        let coolant_pressure_gauge = gauge(
            format!("Coolant Pressure"),
            format!("{} PSI", cp),
            *cp,
            SENSOR_CONSTANTS.coolant_pressure_max,
        );
        frame.render_widget(coolant_pressure_gauge, gauge_chunks[2]);
    });

    connection.voltages.last().map(|v| {
        let voltage_gauge = gauge(
            format!("Volts"),
            format!("{:.2} volts", to_volt(*v)),
            *v,
            SENSOR_CONSTANTS.voltage_max,
        );
        frame.render_widget(voltage_gauge, gauge_chunks[3]);
    });

    connection.rpms.last().map(|rpm| {
        let rpm_gauge = gauge(
            format!("Max RPM"),
            format!("{} rpm", rpm),
            *rpm,
            SENSOR_CONSTANTS.rpm_max,
        );
        frame.render_widget(rpm_gauge, gauge_chunks[4]);
    });

    connection.rssis.last().map(|rssi| {
        let rssi_gauge = gauge(
            format!("Signal"),
            format!("{} rssi", rssi),
            rssi + 100,
            SENSOR_CONSTANTS.rssi_max + 100,
        );

        frame.render_widget(rssi_gauge, gauge_chunks[5]);
    });
}

fn main() -> Result<(), Box<dyn Error>> {
    let args: Vec<String> = env::args().collect();
    let program = args[0].clone();
    let usage = format!("Usage: {} dev [options]", program);

    let port = match &args[..] {
        [_, file] => {
            // let wtf = String::from(f);
            serialport::new(file, 57600).timeout(Duration::from_millis(500)).open().expect("Failed to open port!")
        },
        _ => {
            println!("{}", usage);
            let ports = serialport::available_ports().expect("No ports found!");
            for p in ports {
                println!("{}", p.port_name);
            }
            return Err("No file provided".into())
        }
    };

    let mut connection = match Connection::init(port) {
        Ok(c) => c,
        Err(e) => return Err("Failed to open a connection".into()),
    };

    // App

    let stdout = io::stdout().into_raw_mode()?;
    let stdout = MouseTerminal::from(stdout);
    let stdout = AlternateScreen::from(stdout);
    let backend = TermionBackend::new(stdout);
    let mut terminal = Terminal::new(backend)?;
    let events = Events::new();
    let mut app = BsodApp {
        // add tabs here
        tabs: TabsState::new(vec![
            "Main",
            "Temperatures",
            "Pressures",
            "Voltages",
            "RPMs",
        ]),
    };

    // Main loop
    loop {
        connection.read();

        terminal.draw(|f| {
            let size = f.size();
            let chunks = Layout::default()
                .direction(Direction::Vertical)
                .margin(1)
                .constraints([Constraint::Length(3), Constraint::Min(0)].as_ref())
                .split(size);

            let block = Block::default().style(Style::default().bg(DEFAULT_BG).fg(Color::Gray));
            f.render_widget(block, size);
            let titles = app
                .tabs
                .titles
                .iter()
                .map(|t| {
                    let (first, rest) = t.split_at(1);
                    Spans::from(vec![
                        Span::styled(first, Style::default().fg(Color::LightYellow)),
                        Span::styled(rest, Style::default().fg(Color::Green)),
                    ])
                })
                .collect();
            let tabs = Tabs::new(titles)
                .block(Block::default().borders(Borders::ALL).title("Tabs"))
                .select(app.tabs.index)
                .style(Style::default().fg(Color::Cyan).bg(DEFAULT_BG))
                .highlight_style(
                    Style::default()
                        .add_modifier(Modifier::BOLD)
                        .fg(Color::Black)
                        .bg(Color::LightGreen),
                );
            f.render_widget(tabs, chunks[0]);
            let body = chunks[1];
            let block = Block::default().borders(Borders::ALL).title("Graphs");

            let main_chunks = Layout::default()
                .direction(Direction::Vertical)
                .margin(1)
                .constraints([Constraint::Percentage(75), Constraint::Percentage(25)].as_ref())
                .split(body);

            f.render_widget(block, body);

            let events: Vec<ListItem> = connection
                .events
                .iter()
                .rev()
                .filter(|(_counter, _event, level)| match level {
                    &LogLevel::Error => true,
                    &LogLevel::Warn => true,
                    &LogLevel::Info => false,
                })
                .map(|(counter, event, level)| {
                    // Colorcode the level depending on its type
                    let s = match level {
                        LogLevel::Error => Style::default().fg(Color::Red),
                        LogLevel::Warn => Style::default().fg(Color::Yellow),
                        LogLevel::Info => Style::default().fg(Color::Blue),
                    };

                    let level_label = match level {
                        LogLevel::Error => "Error",
                        LogLevel::Warn => "Warn",
                        LogLevel::Info => "Info",
                    };
                    let header = Spans::from(vec![
                        Span::styled(format!("{:<9}", level_label), s),
                        Span::raw(" "),
                        Span::styled(
                            format!("{}", counter),
                            Style::default().add_modifier(Modifier::ITALIC),
                        ),
                    ]);
                    // The event gets it's own line
                    let log = Spans::from(vec![Span::raw(event)]);

                    // Here several things happen:
                    // 1. Add a `---` spacing line above the final list entry
                    // 3. Add a spacer line
                    // 4. Add the actual event
                    ListItem::new(vec![
                        Spans::from("-".repeat(chunks[1].width as usize)),
                        header,
                        log,
                    ])
                })
                .collect();
            let events_list = List::new(events)
                .block(Block::default().borders(Borders::ALL).title(format!("Event Log [received {} messages, {} errors]", connection.counter, connection.error_counter)))
                .start_corner(Corner::BottomLeft);
            f.render_widget(events_list, main_chunks[1]);

            match app.tabs.index {
                0 => render_gauges(f, main_chunks[0], &connection),
                1 => {
                    let data = vec![connection
                        .oil_temps
                        .last_n(512)
                        .iter()
                        .map(|(i, t)| (i.to_owned() as f64, t.to_owned() as f64))
                        .collect()];

                    let c = chart(
                        main_chunks[0],
                        format!("Temperatures"),
                        "Time",
                        "Temperature (F)",
                        &data,
                        vec![Color::Cyan],
                        vec!["Oil Temp"],
                        100,
                        SENSOR_CONSTANTS.oil_temp_max,
                        40,
                    );

                    f.render_widget(c, main_chunks[0])
                }
                2 => {
                    let data = vec![
                        connection
                            .oil_pressures
                            .last_n(512)
                            .iter()
                            .map(|(i, t)| (i.to_owned() as f64, t.to_owned() as f64))
                            .collect(),
                        connection
                            .coolant_pressures
                            .last_n(512)
                            .iter()
                            .map(|(i, t)| (i.to_owned() as f64, t.to_owned() as f64))
                            .collect(),
                    ];

                    let c = chart(
                        main_chunks[0],
                        format!("Pressures"),
                        "Time",
                        "Pressure (PSI)",
                        &data,
                        vec![Color::Cyan, Color::Red],
                        vec!["Oil Pressure", "Coolant Pressure"],
                        0,
                        SENSOR_CONSTANTS
                            .oil_pressure_max
                            .max(SENSOR_CONSTANTS.coolant_pressure_max),
                        20,
                    );

                    f.render_widget(c, main_chunks[0])
                }
                3 => {
                    let data = vec![connection
                        .voltages
                        .last_n(512)
                        .iter()
                        .map(|(i, v)| (i.to_owned() as f64, to_volt(v.to_owned())))
                        .collect()];

                    let c = chart(
                        main_chunks[0],
                        format!("Voltages"),
                        "Time",
                        "Volts (V)",
                        &data,
                        vec![Color::Green],
                        vec!["Voltage"],
                        10,
                        to_volt(SENSOR_CONSTANTS.voltage_max).ceil() as i16,
                        1,
                    );

                    f.render_widget(c, main_chunks[0])
                }
                4 => {
                    let data = vec![connection
                        .rpms
                        .last_n(512)
                        .iter()
                        .map(|(i, t)| (i.to_owned() as f64, t.to_owned() as f64))
                        .collect()];

                    let c = chart(
                        main_chunks[0],
                        format!("RPM"),
                        "Time",
                        "RPM",
                        &data,
                        vec![Color::Cyan],
                        vec!["RPM"],
                        1000,
                        SENSOR_CONSTANTS.rpm_max,
                        1000,
                    );

                    f.render_widget(c, main_chunks[0])
                }
                _ => panic!("invalid tab?"),
            };
        })?;

        if let Event::Input(input) = events.next()? {
            match input {
                Key::Char('q') => {
                    break;
                }
                Key::Right => app.tabs.next(),
                Key::Left => app.tabs.previous(),
                Key::Char('m') => app.tabs.goto(0),
                Key::Char('t') => app.tabs.goto(1),
                Key::Char('p') => app.tabs.goto(2),
                Key::Char('v') => app.tabs.goto(3),
                Key::Char('r') => app.tabs.goto(4),
                Key::Char('e') => app.tabs.goto(5),
                _ => {}
            }
        }
    }
    Ok(())
}
