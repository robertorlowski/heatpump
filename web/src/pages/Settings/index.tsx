import {Component} from 'preact';
import './style.css';
import '../../api/api'
import { HpRequests } from '../../api/api';
import { TSettings, TSlot } from '../../api/type';


function format(num?: Number) :String {
	if (num == undefined) {
		return "";
	}
	return num.toString().padStart(2, '0');
}

interface IState {
    data?: TSettings;
}

export default class Settings extends Component<{}, IState> {


	componentWillMount() {
		HpRequests.getSettings().then(resp => {
			this.setState({ data: resp });
		   });
	};

	render({}, {}) {
		if (!this.state.data) {
			return 'Loading...';
		}
		return (
			<div class="settings">
				<h2>Główne ustawienia</h2>
				<section>
					<this.resource
						title="Automatyczny start HP"
						description = "Przedziały czasu w którym nastąpi włączenie HP."
						data= {this.state.data.settings}
					/>
					<br/>
					<this.resource
						title="Wyłączenie wykorzystania mocy z PV"
						description = "Przedziały czasu w którym nastąpi wyłączenie weryfikacji wytwarzanej mocy na panelach fotowoltaicznych."
						data = {[this.state.data.night_hour]}
					/>
				</section>
			</div>
		)
	}

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


