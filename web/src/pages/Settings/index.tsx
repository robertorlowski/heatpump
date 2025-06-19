import {Attributes, Component, ComponentChildren, createRef, Ref} from 'preact';
import './style.css';
import '../../api/api'
import { HpRequests } from '../../api/api';
import { TSaveCO, TSettings, TSlot } from '../../api/type';

function format(num?: Number) :String {
	if (num == undefined) {
		return "";
	}
	return num.toString().padStart(2, '0');
}

interface IState {
    data?: TSettings;
	value?: TSaveCO;
	error?: boolean;
}

export default class Settings extends Component<{}, IState> {
	render({}, {}) {
		if ( !this.state.data || !this.state.value) {
			return 'Loading...';
		}
		return (
			
			<div class="settings" >
				<h2>Główne ustawienia</h2>
				<section>
					<div class="resource">
						<h3>Ustaw</h3>
						<hr/>	
						<div style="min-width: 240px;">
							<span class ="label">Tryb pracy:</span>
							<select class="dict-select" 
								onChange={(e) => this.handleSave({work_mode: e.currentTarget.value}) }
								value={this.state.value.work_mode} 
							>
								<option value="M">ręczny</option>
								<option value="A">automatyczny</option>
								<option value="PV">automatyczny z PV</option>
								<option value="CWU">CWU</option>
								<option value="OFF">OFF</option>
							</select>
						</div>
						<div style="min-width: 240px;">
							<span class ="label">Temperatura (min/max):</span>
							<input
								class="temperature"
								type="number"
								name="min"
								value={this.state.value.temperature_co_min}
								onChange={(e) => this.handleSave({
									temperature_co_min: e.currentTarget.value,
									temperature_co_max: this.state.value?.temperature_co_max
								}) }
							/>
							<input
								class="temperature"
								type="number"
								name="max"
								value={this.state.value.temperature_co_max}
								onChange={(e) => this.handleSave({
									temperature_co_min: this.state.value?.temperature_co_min,
									temperature_co_max: e.currentTarget.value
								}) }
							/>
						</div>

						<div style="min-width: 240px;">
							<span class ="label">Wymuś pracę:</span>
							<input
								title="Wymuś pracę:"
								type="checkbox"
								checked={this.state.value.force == "1" ? true : false}
								onChange={(e) => this.handleSave({force: e.currentTarget.checked ? "1": "0"}) }
							/> 
						</div>	
						<div style="min-width: 240px;">
							<span class ="label">Pompa zimnej wody:</span>
							<input
								title="Pompa zimnej wody"
								type="checkbox"
								name="coldPomp"
								checked={this.state.value.cold_pomp == "1" ? true : false}
								onChange={(e) => this.handleSave({cold_pomp: e.currentTarget.checked ? "1": "0"}) }
							/>
						</div>	
						<div style="min-width: 240px;">						
							<span class ="label">Pompa ciepłej wody:</span>
							<input
								title="Pompa ciepłej wody"
								type="checkbox"
								name="hotPomp"
								checked={this.state.value.hot_pomp == "1" ? true : false}
								onChange={(e) => this.handleSave({hot_pomp: e.currentTarget.checked ? "1": "0"}) }
							/> 
						</div>	
						<div style="min-width: 240px;">
							<span class ="label">Grzałka krateru:</span>
							<input
								title="Grzałka krateru"
								name="sumpHeater"
								type="checkbox"
								checked={this.state.value.sump_heater == "1" ? true : false}
								onChange={(e) => this.handleSave({sump_heater: e.currentTarget.checked ? "1": "0"}) }
							/> 
						</div>
						<p>
							<span class={this.state.error ? `error show` : `error hide` }> 
								Wystąpił błąd podczas wykonywania operacji..
							</span>
						</p>	 
					</div>
					<br/>
					<this.resource
						title="Automatyczny start CO"
						description = "Przedziały czasu w którym nastąpi włączenie HP."
						data= {this.state.data.settings}
					/>
					{/* <br/>
					<this.resource
						title="Wyłączenie wykorzystania mocy z PV"
						description = "Przedziały czasu w którym nastąpi wyłączenie weryfikacji wytwarzanej mocy na panelach fotowoltaicznych."
						data = {[this.state.data.night_hour]}
					/> */}
				</section>
			</div>
		)
	}

	componentWillMount() {
		this.setState({ error: false });

		HpRequests.getSettings().then((resp) => {
			this.setState({ data: resp });
		   }
		).catch(err =>{
			console.log(err);
		   }
		);

		HpRequests.getCoData().then((resp) => {
			this.setState(
				{
					value: { 
						force: "0",
						work_mode: resp.work_mode,
						cold_pomp: "0",
						hot_pomp: "0",
						sump_heater: "0",
						temperature_co_min: String(resp.HP.Tmin),
						temperature_co_max: String(resp.HP.Tmax)
					}
				}
			)
		}).catch(err => {
			console.log(err);
		   }
		);
	};

	handleSave(value: TSaveCO) {
		this.setState(
				{
					value: { 
						force: value.force != undefined?  value.force : this.state.value?.force,
						work_mode: value.work_mode  != undefined ? value.work_mode : this.state.value?.work_mode,
						cold_pomp: value.cold_pomp  != undefined ? value.cold_pomp : this.state.value?.cold_pomp,
						hot_pomp:  value.hot_pomp   != undefined? value.hot_pomp : this.state.value?.hot_pomp,
						sump_heater: value.sump_heater  != undefined ? value.sump_heater : this.state.value?.sump_heater,
						temperature_co_min: value.temperature_co_min  != undefined ? value.temperature_co_min : this.state.value?.temperature_co_min,
						temperature_co_max: value.temperature_co_max  != undefined ? value.temperature_co_max : this.state.value?.temperature_co_max
					}
				}
		);
		console.log(this.state.value);

		HpRequests.setCoData(value)
		.then(response => {
			console.log(response);
			this.setState(
				{
					error: response.status == 200 ? false : true
				}
			)
		});
	};

	resource(props: {title, description :String, data: TSlot[] }) {
		let slotList = [];

		for (let index = 0; index < props.data.length; index++) {
			const element = props.data[index];
			slotList.push(<li key={index}> 
				{format(element.slot_start_hour)}:{format(element.slot_start_minute)} <span/>
				<strong>-</strong><span/> {format(element.slot_stop_hour)}:{format(element.slot_stop_minute)}   
			</li>);
		}
	
		return (
			<div class="resource">
				<h3>{props.title}</h3>
				{props.description}
				<hr/>
				<ul>{slotList}</ul>
			</div>			
		);
	}	
	
}


